/***
 Copyright 2022, Elie Goldman Smith

 This program is FREE SOFTWARE: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
***/
#define NO_ESCAPE
#include "fullscreen_main.h"
#include "text-quads.h"
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAXTEXTLEVELS 7
const float TEXT_BOX_SIZES[MAXTEXTLEVELS] = {3, 4, 5, 7, 9, 11, 14}; // in 'em' units

typedef struct {
 float x,y;
 float dx,dy;
 float size;
 unsigned char r,g,b,a;
 char *text;
 int nTextLevels;
 TQ_Drawable textRenders[MAXTEXTLEVELS];
} Node;
#define MAXNODES 8192
int nNodes=0;
Node nodes[MAXNODES];

typedef struct {
 int from, to;
} Link;
#define MAXLINKS 16384
int nLinks=0;
Link links[MAXLINKS];

int focus = 0; // index of node that is in focus (selected as 1st node)
int mark  =-1; // index of node that is marked   (selected as 2nd node)
int toDrag=-1; // index of node being dragged during dragging

const    char *          monitorFileName = "/tmp/edit-text-node";
volatile struct timespec monitorFileTime = {0};
volatile int             monitorEditNode = -1; // index of node being edited

TQ_Drawable helpRender = {0};
#define HELP_TEXT  "CONTROLS\n----\nN: new node\nE: edit text\nLeft Click: select node\nSPACE: mark node\nC: connect nodes\nD: disconnect nodes\nF: connect nodes but flipped\nINSERT: add intermediate node\nDELETE: delete current node\n+: new orphaned node\nCTRL-S: Save\nCTRL-O: Open\nT: show current filename\nESC: quit"

TQ_Drawable messageRender = {0};
int messageTimeout = 0;

TQ_Drawable dialogRender = {0};

char filename[FILENAME_MAX]=""; // XXX: should it be FILENAME_MAX+1? if so, gotta change it other places too
int isModified = 0;




void addNodeFrom(int id) {
 if (nNodes >= MAXNODES) return;
 if (nLinks >= MAXLINKS) return;
 //focus=nNodes;
 nodes[nNodes].x = _mouse_x;
 nodes[nNodes].y = _mouse_y;
 nodes[nNodes].size = nodes[id].size + RND()*0.02f;
 int lum;
 lum = nodes[id].r + (rand()&255)-128; if(lum<0)lum=0; if(lum>255)lum=255; nodes[nNodes].r = lum;
 lum = nodes[id].g + (rand()&255)-128; if(lum<0)lum=0; if(lum>255)lum=255; nodes[nNodes].g = lum;
 lum = nodes[id].b + (rand()&255)-128; if(lum<0)lum=0; if(lum>255)lum=255; nodes[nNodes].b = lum;
 nodes[nNodes].a = 255;
 links[nLinks].from = id;
 links[nLinks].to   = nNodes;
 nLinks++;
 nNodes++;
}

void connectNodes(int from, int to) {
 if (to<0 || from<0 || to==from) return; // invalid connection
 for (int i=0; i<nLinks; i++) {
  if (links[i].to==to   && links[i].from==from) return; // already connected
  if (links[i].to==from && links[i].from==to)
     {links[i].to= to   ;  links[i].from= from; return;}// flipped direction
 }
 if (nLinks >= MAXLINKS) return; // too many links
 links[nLinks].to   = to;
 links[nLinks].from = from;
 nLinks++; // added connection (main case)
}

void disconnectNodes(int from, int to) {
 for (int i=0; i<nLinks; i++) {
  if ((links[i].to==to   && links[i].from==from)
  ||  (links[i].to==from && links[i].from==to)) {
   links[i] = links[--nLinks];
   return; // deleted connection (main case)
  }
 }
}

int nodeNearest(float x, float y) {
 int which = 0;
 float lowest = 1e36;
 for (int i=0; i<nNodes; i++) {
  float dsq = (nodes[i].x - x)*(nodes[i].x - x) + (nodes[i].y - y)*(nodes[i].y - y);
  if (dsq < lowest) { lowest=dsq; which=i; }
 }
 return which;
}

void eraseNodeText(int id) {
 free(nodes[id].text);
 nodes[id].text = NULL;
 for (int tl=0; tl < nodes[id].nTextLevels; tl++) tq_delete(&nodes[id].textRenders[tl]);
}

void deleteNode(int id) {
 eraseNodeText(id);
 nodes[id] = nodes[--nNodes];
 memset(&nodes[nNodes], 0, sizeof(Node));
 for (int i=0; i<nLinks; i++) {
  if (links[i].to==id || links[i].from==id) links[i--] = links[--nLinks];
  else {
   if (links[i].to == nNodes) links[i].to = id;
   if (links[i].from==nNodes) links[i].from=id;
  }
 }
 #define UR(ref)   if (ref==id) ref=-1; else if (ref==nNodes) ref=id;  // "UR" stands for "update reference"
 UR(mark);
 UR(focus);
 UR(toDrag);
 UR(monitorEditNode);
 #undef UR
}

void genNodeTextRenders(int id) {
 int tl=0;
 while (tl<MAXTEXTLEVELS) {
  nodes[id].textRenders[tl++] = tq_centered_fitted(nodes[id].text, TEXT_BOX_SIZES[tl], TEXT_BOX_SIZES[tl]);
  if ((_tq_flags & TQ_FLAG_COMPLETE)) break;
 }
 nodes[id].nTextLevels = tl;
}




void message(const char *str) {
 tq_delete(&messageRender);
 messageRender = tq_line_centered(str);
 messageTimeout = 1000; // frames
}

int message_printf(const char *fmt, ...) {
 va_list args;
 va_start(args, fmt);
 size_t size;
 char *str;
 FILE *ss = open_memstream(&str, &size);
 if (!ss) return 0;
 int n = vfprintf(ss, fmt, args);
 fclose(ss);
 va_end(args);
 message(str);
 free(str);
 return n;
}




void saveToFile(const char *filename) {
 FILE *f = fopen(filename,"w");
 if (!f) return; // TODO: handle error case
 fprintf(f, "view:\nf=%d\nnodes:\n", focus);
 for (int i=0; i<nNodes; i++) {
  fprintf(f, "i=%d c=%02X%02X%02X t=\"", i, (int)nodes[i].r, (int)nodes[i].g, (int)nodes[i].b);
  char *p = nodes[i].text;
  if (p) {
   while (*p) {
    if     (*p=='\n') fputs("\\n", f);
    else if(*p=='\"') fputs("\\\"",f);
    else fputc(*p, f);
    p++;
   }
  }fprintf(f,"\"\n");
 }
 fprintf(f,"connections:\n");
 for (int i=0; i<nLinks; i++) fprintf(f, "a=%d b=%d\n", links[i].from, links[i].to);
 fclose(f);
 printf("Saved to file %s\n", filename);
}

void saveAs() {
 char fn[FILENAME_MAX];
 FILE *f = popen("zenity --file --title 'SAVE AS...' --save --confirm-overwrite 'Overwrite existing file?' --maximized --on-top", "r");
 if (f) {
  char *c = fgets(fn,FILENAME_MAX,f);
  if (pclose(f)==0) {
   int len=strlen(fn);
   if (len>1) {
    fn[len-1] = 0; // to remove the newline
    strcpy(filename, fn);
    saveToFile(filename);
    message_printf("Saved to %s\n", filename);
   }
   else puts("Empty filename");
  }
  else puts("No filename selected");
 }
}

void save() {
 if (!filename[0]) saveAs(); // untitled
 else {
  saveToFile(filename);
  message_printf("Saved to %s\n", filename);
 }
}



int loadFile(const char *filename) { // TODO: respond more robustly (i.e. to avoid segfault when trying to load an invalid file)
 int success = 0;
 FILE *f = fopen(filename, "r");
 if (f) {
  if (fscanf(f,"view:\nf=%d\nnodes:\n",&focus)>0) {
   // clear existing data
   for (int i=0; i<nNodes; i++) {
    eraseNodeText(i);
    nodes[i].x = RND();
    nodes[i].y = RND();
   }
   nNodes = nLinks = 0;
   // read nodes from file
   for (int i=0; i<MAXNODES; i++) {
    int id; int r,g,b; char c;
    if (fscanf(f, "i=%d c=%02X%02X%02X t=\"", &id, &r, &g, &b)>0) {
     if (id >= 0 && id < MAXNODES) {
      nodes[id].r=r; nodes[id].g=g; nodes[id].b=b; nodes[i].a=255;
      if (nNodes <= id) nNodes = id+1;
      size_t size;
      FILE *ss = open_memstream(&nodes[id].text, &size);
      while (1) {
       c = fgetc(f);
       if (c == '\"' || c == EOF) break;
       if (c == '\\') {
        c = fgetc(f);
        if      (c == EOF) break;
        if      (c == '\"') fputc('\"',ss);
        else if (c == 'n' ) fputc('\n',ss);
        else if (c == '\\') fputc('\\',ss);
        else {
         fputc('\\',ss);
         fputc(c, ss);
        }
       } else fputc(c, ss);
      } fclose(ss);
     }
    } else i=MAXNODES; // reached the line "connections:" so end this for-loop
    while ((c=fgetc(f)) != '\n' && c != EOF); // skip to the next line
   }
   // read the list of connections from file
   for (int i=0; i<MAXLINKS; i++, nLinks++) {
    int a,b;
    if (fscanf(f,"a=%d b=%d\n",&a,&b)==2) {
     links[i].from = a;
     links[i].to   = b;
    } else break;
   } success=1;
  }
  fclose(f);
  if (success) {
   for (int i=0; i<nNodes; i++) genNodeTextRenders(i); // this is done at the end because it might need a lot of memory (fclose() would have freed some)
   printf("Opened file %s\n", filename);
   isModified=0;
  } else printf("Invalid file %s\n", filename);
 } else perror(filename); // XXX: i dont like the inconsistancy of what goes to stdout vs stderr vs main screen. Also the inconsistancy of which functions are responsible for such printing (like what about the puts() calls in draw()). Need to decide on a proper schema for this.
 return success;
}




void drawCircle(float x, float y, float radius) {
 glBegin(GL_LINE_LOOP);
 float da=(float)M_PI/16.f;
 for (float a = da*0.5f; a < (float)M_PI*2.f; a += da) glVertex2f(x + cosf(a)*radius, y + sinf(a)*radius);
 glEnd();
}




void editTextNode(int id) {
 const char *editor_names[] = {"leafpad","defaulttexteditor","gedit","geany","notepad++","wordpad","notepad","nano","vim","vi","emacs",NULL};
 FILE *f = fopen(monitorFileName, "w");
 if (f) {
  if (nodes[id].text) fputs(nodes[id].text, f);
  fclose(f);
  struct stat st;
  stat(monitorFileName, &st);
  monitorFileTime = st.st_mtim;
  monitorEditNode = id;
  if (fork()==0) {
   for (int i=0; editor_names[i]; i++) execlp(editor_names[0], editor_names[0], monitorFileName, (char*)NULL);
   printf("Can't edit node: No text editor found.\n");
  }
 }
 else {} // TODO: handle error case
}

void *fileMonitor(void *ptr) { // pthread
 while (1) {
  if (monitorEditNode >= 0) {
   struct stat st;
   stat(monitorFileName, &st);
   if (st.st_mtim.tv_sec != monitorFileTime.tv_sec || st.st_mtim.tv_nsec != monitorFileTime.tv_nsec) {
    char *str = malloc(st.st_size+1);
    if (!str) return NULL;        // TODO: handle error case
    FILE *f = fopen(monitorFileName, "r");
    if(!f){free(str);return NULL;}// TODO: handle error case
    int n = fread(str, 1, st.st_size, f);
    str[n]=0;
    fclose(f);
    eraseNodeText(monitorEditNode);
    nodes[monitorEditNode].text = str;
    genNodeTextRenders(monitorEditNode);
    monitorFileTime = st.st_mtim;
    monitorEditNode = -1;
   }
  } sleep(1);
 }
}









void init() {
 tq_init(); glDisable(GL_TEXTURE_2D);
 show_mouse();
 memset(nodes, 0, sizeof(nodes)); // this also initializes any pointers to NULL, so it's safe to call free() on them at any time
 memset(links, -1,sizeof(links)); // -1 is safe, will be interpereted as 'not a link'
 if (_global_argc==2) {
  loadFile(_global_argv[1]);
  strcpy(filename, _global_argv[1]);
  // message_printf("Opened file: %s", filename);
 }
 if (nNodes < 1) {
  // add root node
  nodes[nNodes].size = 0.04f;
  nodes[nNodes].r = 255;
  nodes[nNodes].g = 255;
  nodes[nNodes].b = 255;
  nodes[nNodes].a = 255;
  nNodes++;
 }
 pthread_t fm; // file monitor thread (for editing a node)
 pthread_create(&fm,NULL,fileMonitor,NULL);
 #ifdef USE_MULTISAMPLING
 glLineWidth(1.5f);
 #endif
 dialogRender = tq_centered_fitted("Save changes to current file first?\nY-yes  N-no  C-cancel", 128.f, 128.f); // XXX: maybe have 2 different dialogs for 'open' vs 'quit'? Maybe write it differently i.e. "quit without saving"/"save and quit"/"cancel"?
 message("Press/hold F1 for help\n");
}


void draw() {
 static int state=0;
 if (state==1 || state==4) { // The 'Save changes?' dialog:
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glPushAttrib(GL_ENABLE_BIT);
  tq_mode();
  glColor3ub(255,255,255);
  glPushMatrix();
  glLoadIdentity();
  glScalef((1.f/128.f), (1.f/128.f)*_screen_x/_screen_y, 1.f);
  tq_draw(dialogRender);
  tq_draw(dialogRender); // TODO: instead of drawing twice, use high-contrast shader
  glPopMatrix();
  glPopAttrib();
  if (keymap['Y']==KEY_FRESHLY_PRESSED) state += 1;
  if (keymap['N']==KEY_FRESHLY_PRESSED) state += 2;
  if (keymap['C']==KEY_FRESHLY_PRESSED) state = 0;
  if (keymap[ 27]==KEY_FRESHLY_PRESSED) state = 0;
  return;
 }

 //==User interface==

 // select node (Left click)
 if (_mouse_button_map[0]==KEY_FRESHLY_PRESSED) focus = nodeNearest(_mouse_x, _mouse_y);

 // drag node (Right click)
 if (_mouse_button_map[2]==KEY_FRESHLY_PRESSED) toDrag = nodeNearest(_mouse_x, _mouse_y);
 if (_mouse_button_map[2]) {
  if (toDrag >= 0) {
   nodes[toDrag].x = _mouse_x;
   nodes[toDrag].y = _mouse_y;
  }
 } else toDrag = -1;

 // mark current node (Spacebar)
 if (keymap[' ']==KEY_FRESHLY_PRESSED) {
  if (mark == focus) mark = -1;
  else mark = focus;
 }

 // new node (N)
 if (keymap['N']==KEY_FRESHLY_PRESSED) { addNodeFrom(focus); isModified=1; }

 // edit node text (E)
 if (keymap['E']==KEY_FRESHLY_PRESSED) { editTextNode(focus); isModified=1; }

 // connect/disconnect nodes (C for connect, F for connect flipped, D for disconnect)
 if (keymap['C']==KEY_FRESHLY_PRESSED) { connectNodes   (mark, focus); isModified=1; }
 if (keymap['D']==KEY_FRESHLY_PRESSED) { disconnectNodes(mark, focus); isModified=1; }
 if (keymap['F']==KEY_FRESHLY_PRESSED) { connectNodes   (focus, mark); isModified=1; }

 // insert node between (Insert)
 if (special_keymap[GLUT_KEY_INSERT]==KEY_FRESHLY_PRESSED && focus >= 0 && mark >= 0 && nNodes < MAXNODES) {
  int id = nNodes++;
  nodes[id].x = 0.5f*(nodes[mark].x + nodes[focus].x);
  nodes[id].y = 0.5f*(nodes[mark].y + nodes[focus].y);
  nodes[id].r = 0.5f*(nodes[mark].r + nodes[focus].r);
  nodes[id].g = 0.5f*(nodes[mark].g + nodes[focus].g);
  nodes[id].b = 0.5f*(nodes[mark].b + nodes[focus].b);
  nodes[id].a = 0.5f*(nodes[mark].a + nodes[focus].a);
  disconnectNodes(mark, focus);
  connectNodes(mark, id);
  connectNodes(id, focus);
  // focus = id;
  isModified=1;
 }

 // delete node (Delete)
 if (keymap[127]==KEY_FRESHLY_PRESSED && nNodes>1) {
  deleteNode(focus);
  focus = nodeNearest(0,0);
  isModified=1;
 }

 // new orphaned node (+)
 if (keymap['+']==KEY_FRESHLY_PRESSED && nNodes < MAXNODES) {
  int id = nNodes++;
  nodes[id].x = _mouse_x;
  nodes[id].y = _mouse_y;
  nodes[id].r = nodes[id].g = nodes[id].b = nodes[id].a = -1;
  focus = id;
  editTextNode(id);
  isModified=1;
 }

 // adjust node color (R,G,B; combined with '-' or '=')
 int colorDelta = 17 * (!!keymap['='] - !!keymap['-']);
 if (colorDelta) {
  if (keymap['R']) {
   int l = nodes[focus].r + colorDelta;  if (l<0) l=0; else if (l>255) l=255;
   nodes[focus].r = l;                   isModified=1;
  }
  if (keymap['G']) {
   int l = nodes[focus].g + colorDelta;  if (l<0) l=0; else if (l>255) l=255;
   nodes[focus].g = l;                   isModified=1;
  }
  if (keymap['B']) {
   int l = nodes[focus].b + colorDelta;  if (l<0) l=0; else if (l>255) l=255;
   nodes[focus].b = l;                   isModified=1;
  }
 }
 
 // select a node using arrow keys
 static float selectorx = 0.f, selectory = 0.f;
 if (special_keymap[GLUT_KEY_LEFT]||special_keymap[GLUT_KEY_RIGHT]||special_keymap[GLUT_KEY_DOWN]||special_keymap[GLUT_KEY_UP]) {
  if (special_keymap[GLUT_KEY_LEFT ]) selectorx -= 0.02f;
  if (special_keymap[GLUT_KEY_RIGHT]) selectorx += 0.02f;
  if (special_keymap[GLUT_KEY_DOWN ]) selectory -= 0.02f;
  if (special_keymap[GLUT_KEY_UP   ]) selectory += 0.02f;
  focus = nodeNearest(selectorx, selectory);
 } else selectorx = selectory = 0.f; 

 // adjust graph directionality (J)
 static float directionalityX = 0.f;
 static float directionalityY = 0.f;
 if (keymap['J']==KEY_FRESHLY_PRESSED) {
  static int d=0;
  if (++d > 2) d=0;
  if      (d==0) {
   directionalityX = 0.f;     directionalityY = 0.f;
   message("Flow directionality: None");
  }else if(d==1) {
   directionalityX = 0.f;     directionalityY = -0.0004f;
   message("Flow directionality: Down");
  }else if(d==2) {
   directionalityX = 0.0004f; directionalityY = 0.f;
   message("Flow directionality: Right");
  }
 }

 // adjust bubble effect aka "space curvature" (K)
 static float bubbleEffect = 0.14f;
 static float relevanceRange = 4.f;
 if (keymap['K']==KEY_FRESHLY_PRESSED) {
  static int b=1;
  if (++b > 2) b=0;
  if      (b==0) {
   bubbleEffect = 0.00f;
   relevanceRange= 12.f;
   message("Bubble effect: None (flat)");
  }else if(b==1) {
   bubbleEffect = 0.14f;
   relevanceRange = 4.f;
   message("Bubble effect: Moderate");
  }else if(b==2) {
   bubbleEffect = 0.25f;
   relevanceRange = 3.f;
   message("Bubble effect: High");
  }
 }
 float mrr = (float)_screen_x/_screen_y + (float)_screen_y/_screen_x;
 if (relevanceRange < mrr) relevanceRange = mrr; // minimum relevance range for making sure nothing onscreen disappears


 // help (F1)
 if (special_keymap[GLUT_KEY_F1]) {
  static int width=-1, height=-1;
  static float   w=-1,      h=-1;
  if (_screen_x != width || _screen_y != height) {
   width = _screen_x;
   height= _screen_y;
   w = width *(1.f/_screen_size);
   h = height*(1.f/_screen_size);
   do {
    w *= 2.f; h *= 2.f;
    tq_delete(&helpRender);
    helpRender = tq_centered_fitted(HELP_TEXT, w, h);
   } while (!(_tq_flags & TQ_FLAG_COMPLETE));
  }
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  glPushMatrix();
  glLoadIdentity();
  glScalef(2.f/w, 2.f/h, 1.f);
  glPushAttrib(GL_ENABLE_BIT); tq_mode(); glBlendEquation(GL_FUNC_ADD);
  glColor3f(1.f, 1.f, 0.f);
  tq_draw(helpRender);
  tq_draw(helpRender); // TODO: instead of drawing twice, use high-contrast shader
  glPopAttrib();
  glPopMatrix();
  return; // Skip the rest of rendering
 }

 // save file (Ctrl-S or F5)
 if (keymap[19]==KEY_FRESHLY_PRESSED || (special_keymap[114] && keymap['S']==KEY_FRESHLY_PRESSED) || special_keymap[GLUT_KEY_F5]==KEY_FRESHLY_PRESSED) {
  if (special_keymap[112] || special_keymap[113]) saveAs(); // Ctrl-Shift-S to force 'save as'
  else save();
 }

 // load file (Ctrl-O or F6)
 if (keymap[15]==KEY_FRESHLY_PRESSED || (special_keymap[114] && keymap['O']==KEY_FRESHLY_PRESSED) || special_keymap[GLUT_KEY_F6]==KEY_FRESHLY_PRESSED) {
  if (isModified)state = 1; // dialog
  else           state = 3; // bypass dialog
 }
 if (state==2 || state==3) {// 2 for 'yes' to saving changes, 3 for 'no'
  if (state==2) save();     // XXX: in this current implementation, if the user hits 'cancel' on the 'save as' dialog, it still shows the 'open' dialog immediately after, and is able to open a new file without saving changes to the current one. Is this correct behavior? Maybe not - it should probably cancel both the open and the save.     Similarly, if the user hits 'cancel' on the 'open' dialog, it still saves the file anyway. Is that correct, or should it cancel both the open and the save?
  char fn[FILENAME_MAX];
  FILE *f = popen("zenity --file --title 'Open graph file...' --maximized --close-on-unfocus", "r"); // or could also use --on-top
  if (f) {
   char *c = fgets(fn,FILENAME_MAX,f);
   if (pclose(f)==0) {
    int len=strlen(fn);
    if (len>1) {
     fn[len-1] = 0; // to remove the newline
     if (loadFile(fn)) {
      message_printf("Opened file: %s", fn);
      strcpy(filename, fn);
     }
     else message_printf("Invalid file '%s' - did not load", fn);
    }
    else puts("Empty filename");
   }
   else puts("No filename selected");
  }
  state = 0;
 }
 
 // show current filename (T)
 if (keymap['T']==KEY_FRESHLY_PRESSED) message_printf("%s%s", filename[0]?filename:"[untitled]", isModified?" (modified)":"");

 // quit (ESC)
 if (keymap[27]==KEY_FRESHLY_PRESSED) {
  if (isModified)state = 4; // dialog
  else           state = 6; // bypass dialog
 }
 if (state==5 || state==6) {// 5 for 'yes' to saving changes, 6 for 'no'
  if (state==5) save();
  _exit_the_program = 1;
 }




 //==Physics==
 // center the graph
 if (!_mouse_button_map[2]) {
  static float dx=0; dx *= 0.875f; dx += nodes[focus].x / -128;
  static float dy=0; dy *= 0.875f; dy += nodes[focus].y / -128;
  for (int i=0; i<nNodes; i++) {
   nodes[i].x += dx;
   nodes[i].y += dy;
  }
  if (selectorx || selectory) { selectorx += dx; selectory += dy; }
 }
 // establish which nodes are "relevant" aka potentially onscreen and able to repel other nodes
 static Node* r[MAXNODES];
 int nRelevant=0;
 for (int i=0; i<nNodes; i++) {
  if (nodes[i].x*nodes[i].x + nodes[i].y*nodes[i].y < relevanceRange) {
   r[nRelevant++] = &nodes[i];
   nodes[i].size=0.5f;
  }
 }
 // apply bond forces
 for (int i=0; i<nLinks; i++) {
  float dx = nodes[links[i].to].x - nodes[links[i].from].x;
  float dy = nodes[links[i].to].y - nodes[links[i].from].y;
  float inv = 0.002f / sqrtf(dx*dx+dy*dy+1.f);
  dx *= inv; dy *= inv;
  dx -= directionalityX; // bias to make the graph flow in one direction slightly
  dy -= directionalityY;
  nodes[links[i].to  ].dx -= dx;
  nodes[links[i].to  ].dy -= dy;
  nodes[links[i].from].dx += dx;
  nodes[links[i].from].dy += dy;
 }
 // apply repel forces
 for (int h = 0; h<nRelevant; h++) {
  for (int i=h+1; i<nRelevant; i++) {
   float dx = r[i]->x - r[h]->x;
   float dy = r[i]->y - r[h]->y;
   float maxsize = fabs(dx) > fabs(dy) ? fabs(dx) : fabs(dy); maxsize *= 0.44f;
   if (r[h]->size > maxsize) r[h]->size = maxsize;
   if (r[i]->size > maxsize) r[i]->size = maxsize;
   if (maxsize < 0.0001f) { r[i]->x += RND()*0.0001f; r[i]->y += RND()*0.0001f; }
   float dsq = dx*dx+dy*dy;
   float inv = 1.f/sqrtf(dsq + 0.01f); // for normalizing       (+ bias to avoid singularities)
   inv *= 0.00001f*inv*inv - 0.000008f;// for inverse square law(+ bias to prevent orphaned nodes from drifting off to far)
   float falloff = 1.f - bubbleEffect*(r[i]->x*r[i]->x + r[i]->y*r[i]->y + r[h]->x*r[h]->x + r[h]->y*r[h]->y); if(falloff<0.f)falloff=0.f; // XXX: would it maybe improve performance to move this to the start of the loop with: if(falloff<0.f)continue;
   inv *= falloff*falloff;             // for clustering in distance
   dx *= inv; dy *= inv;               // apply
   r[h]->dx -= dx;
   r[h]->dy -= dy;
   r[i]->dx += dx;
   r[i]->dy += dy;
  }
 }
 // update positions
 for (int i=0; i<nNodes; i++) {
  nodes[i].x += nodes[i].dx;
  nodes[i].y += nodes[i].dy;
  nodes[i].dx *= 0.9375f;
  nodes[i].dy *= 0.9375f;
 }


 //==Rendering==
 glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

 // this projection matrix gives us "aspect-ratio-independent" normalized coordinates instead of the standard "normalized device coordinates"
 glMatrixMode(GL_PROJECTION);
 glLoadIdentity();
 glScalef((GLfloat)_screen_size/(GLfloat)_screen_x, (GLfloat)_screen_size/(GLfloat)_screen_y, 1.f);

 // draw the links
 glBegin(GL_LINES);
 glColor3f(1.f,1.f,1.f);
 for (int i=0; i<nLinks; i++) {
  if (links[i].to >= 0 && links[i].from >= 0) {
   // main line
   glVertex2f(nodes[links[i].to  ].x, nodes[links[i].to  ].y);
   glVertex2f(nodes[links[i].from].x, nodes[links[i].from].y);
   // add subtle chevron at the midpoint of the line, to indicate direction
   float mx =(nodes[links[i].to].x + nodes[links[i].from].x)*0.5f;
   float my =(nodes[links[i].to].y + nodes[links[i].from].y)*0.5f;
   float dx = nodes[links[i].to].x - nodes[links[i].from].x;
   float dy = nodes[links[i].to].y - nodes[links[i].from].y;
   float norm = 0.007f / sqrtf(dx*dx + dy*dy);
   dx *= norm;
   dy *= norm;
   glVertex2f(mx-dy-dx, my+dx-dy);
   glVertex2f(mx   +dx, my   +dy);
   glVertex2f(mx   +dx, my   +dy);
   glVertex2f(mx+dy-dx, my-dx-dy);
  }
 }
 glEnd();

 // precalculate screen boundaries
 float bx = _screen_x / _screen_size;
 float by = _screen_y / _screen_size;

 // draw the nodes
 for (int i=0; i<nRelevant; i++) { // this implementation uses Immediate Mode. XXX: instead of this, maybe use a vertex array with GL_POINTS, use point sprites with a shader that makes the rounded square shape?
  if (r[i]->size > 0) {
   float x1 = r[i]->x-r[i]->size;
   float y1 = r[i]->y-r[i]->size;
   float x2 = r[i]->x+r[i]->size;
   float y2 = r[i]->y+r[i]->size;
   float c  = r[i]->size*0.57f; if (c>0.01f) c=0.01f; // corner size
   if (x1<bx && x2>-bx && y1<by && y2>-by) {
    glBegin(GL_POLYGON);
    glColor3ub(r[i]->r, r[i]->g, r[i]->b);
    glVertex2f(x2-c, y2  );
    glVertex2f(x1+c, y2  );
    glVertex2f(x1  , y2-c);
    glVertex2f(x1  , y1+c);
    glVertex2f(x1+c, y1  );
    glVertex2f(x2-c, y1  );
    glVertex2f(x2  , y1+c);
    glVertex2f(x2  , y2-c);
    glEnd();
   }
  }
 }
 
 // draw the text on the nodes
 glPushAttrib(GL_ENABLE_BIT); tq_mode();
 glColor3ub(255,255,255);
 for (int i=0; i<nRelevant; i++) {
  // filter out nodes with nothing to show
  if (r[i]->textRenders[0].n <= 0) continue;
  if (r[i]->x - r[i]->size  >  bx) continue;
  if (r[i]->x + r[i]->size  < -bx) continue;
  if (r[i]->y - r[i]->size  >  by) continue;
  if (r[i]->y + r[i]->size  < -by) continue;
  // decide which textRender to use, if any.
  const float FONT_SIZE = 0.017f; // (nominal minimum)
  int tl = r[i]->nTextLevels-1;
  while(tl >= 0 && TEXT_BOX_SIZES[tl]*FONT_SIZE*0.5f > r[i]->size) tl--;   // XXX: in cases where text is very short (say, 1 or 2 chars), this implementation hides the text too readily, because it's hiding based on nominal text size instead of actual text size. If I want to change this, I'd have to refactor text-quads.h::tq_centered_fitted() to also return data on how much scaling was done for making it "fitted".
  if   (tl >= 0) {
   // decide whether to make the text black or white
   if (0.2126f*r[i]->r + 0.7152f*r[i]->g + 0.0722f*r[i]->b > 144) glBlendEquation(GL_FUNC_REVERSE_SUBTRACT); else glBlendEquation(GL_FUNC_ADD);
   // render
   glPushMatrix();
   glTranslatef(r[i]->x, r[i]->y, 0.f);
   float scale = r[i]->size * 2.f / TEXT_BOX_SIZES[tl];
   glScalef(scale, scale, 1.f);
   tq_draw(r[i]->textRenders[tl]);
   tq_draw(r[i]->textRenders[tl]); // TODO: instead of drawing twice, make a higher-contrast shader in text-quads.h
   glPopMatrix();
  }
 }
 // draw any message text (top of screen)
 if (messageTimeout > 0) {
  glBlendEquation(GL_FUNC_ADD);
  glColor3f(messageTimeout*0.003f, messageTimeout*0.002f, messageTimeout*0.001f);
  glPushMatrix();
  glTranslatef(0.f, by-0.02f, 0.f);
  glScalef(0.04f, 0.04f, 1.f);
  tq_draw(messageRender);
  glPopMatrix();
  messageTimeout--;
 }
 glPopAttrib();

 // highlight focused node
 if (focus >= 0) {
  glColor3f(1.0f, 1.0f, 0.0f);
  drawCircle(nodes[focus].x, nodes[focus].y, nodes[focus].size*(float)M_SQRT2);
 }

 // highlight marked node
 if (mark >= 0) {
  glColor3f(1.0f, 0.0f, 0.0f);
  drawCircle(nodes[mark].x, nodes[mark].y, nodes[mark].size*(float)M_SQRT2);
 }

 // highlight node being edited
 if (mark >= 0) {
  glColor3f(0.5f, 0.0f, 1.0f);
  drawCircle(nodes[monitorEditNode].x, nodes[monitorEditNode].y, nodes[monitorEditNode].size*(float)M_SQRT2);
 }

 // selector
 if (selectorx || selectory) {
  glColor3f(0.f, 1.f, 0.f);
  drawCircle(selectorx, selectory, 0.02f);
 }
}


void done() {
 for (int i=0; i<nNodes; i++) eraseNodeText(i);
 tq_delete(&helpRender);
 tq_delete(&messageRender);
 tq_delete(&dialogRender);
 tq_done();
}
