// text-quads.h
// Functions for drawing text in OpenGL, using textured quads.
#define TQ_TEXTURE_WIDTH 1004
#define TQ_TEXTURE_HEIGHT 19
#define TQ_TEXTURE_FILENAME "font.data-uint8-1004x19" // DEPENDENCY: This texture file.

/*
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
*/

#include <string.h>
typedef struct { float x, y, tx, ty; } TQ_Vertex; // XXX: maybe use 16-bit snorm instead of 32-bit float?
typedef struct { TQ_Vertex *v; int n;} TQ_Drawable;
TQ_Vertex _tq_alphabet[1024]; // 256 quads (one for every char value)
GLuint    _tq_texture;
unsigned  _tq_flags=0;
#define    TQ_FLAG_COMPLETE 1


void tq_init() {
 // load font bitmap
 size_t SIZE = TQ_TEXTURE_WIDTH*TQ_TEXTURE_HEIGHT;
 unsigned char *bitmap = malloc(SIZE);
 if (!bitmap) return; // TODO: handle error better
 FILE *f = fopen(TQ_TEXTURE_FILENAME,"r");
 if (!f) { perror(TQ_TEXTURE_FILENAME); return; } // TODO: handle error better
 int n = fread(bitmap, 1, SIZE, f);
 if (n != SIZE) { printf("read %d chars\n", n); fclose(f); return; } // TODO: handle error better
 fclose(f);

 // create font texture
 glGenTextures(1, &_tq_texture);
 glEnable(GL_TEXTURE_2D);
 glActiveTexture(GL_TEXTURE0);
 glBindTexture(GL_TEXTURE_2D, _tq_texture);
 glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TQ_TEXTURE_WIDTH, TQ_TEXTURE_HEIGHT-1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, &bitmap[TQ_TEXTURE_WIDTH]); // we skip first row of bitmap because that's the indicator for where each character is.  XXX: maybe use a smaller internalformat? we really only need 4-bit monochrome
 glGenerateMipmap(GL_TEXTURE_2D);

 // create the alphabet
 memset(_tq_alphabet, 0, 1024*sizeof(TQ_Vertex));
 int c = 32; // bitmapped font starts at char 32 (space character)
 int lasti = 0;
 for (int i = 0; i<=TQ_TEXTURE_WIDTH; i++) {
  if (bitmap[i]||i==TQ_TEXTURE_WIDTH) {
   float x = (float)(i-lasti)/TQ_TEXTURE_HEIGHT;
   float tx =         (i-0.5f)/TQ_TEXTURE_WIDTH;
   float lasttx = (lasti+0.5f)/TQ_TEXTURE_WIDTH;
   lasti = i;
   _tq_alphabet[c*4  ].x = x;
   _tq_alphabet[c*4  ].y = -1.f;
   _tq_alphabet[c*4  ].tx= tx;
   _tq_alphabet[c*4  ].ty= 1;
   _tq_alphabet[c*4+1].x = x;
   _tq_alphabet[c*4+1].y = 0.f;
   _tq_alphabet[c*4+1].tx= tx;
   _tq_alphabet[c*4+1].ty= 0;
   _tq_alphabet[c*4+2].x = 0;
   _tq_alphabet[c*4+2].y = 0.f;
   _tq_alphabet[c*4+2].tx= lasttx;
   _tq_alphabet[c*4+2].ty= 0;
   _tq_alphabet[c*4+3].x = 0;
   _tq_alphabet[c*4+3].y = -1.f;
   _tq_alphabet[c*4+3].tx= lasttx;
   _tq_alphabet[c*4+3].ty= 1;
   c++;
  }
 }
 free(bitmap);
}



void tq_mode() {
 glEnable(GL_TEXTURE_2D);
 glActiveTexture(GL_TEXTURE0);
 glBindTexture(GL_TEXTURE_2D, _tq_texture);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
 glEnable(GL_BLEND);
 glBlendFunc(GL_ONE, GL_ONE);
 glEnableClientState(GL_VERTEX_ARRAY);
 glEnableClientState(GL_TEXTURE_COORD_ARRAY);
}



void tq_draw(TQ_Drawable td) {
 if (!td.v || !td.n) return;
 glVertexPointer  (2, GL_FLOAT, sizeof(TQ_Vertex), &td.v[0].x);
 glTexCoordPointer(2, GL_FLOAT, sizeof(TQ_Vertex), &td.v[0].tx);
 glDrawArrays(GL_QUADS, 0, td.n);
}


TQ_Drawable tq_line_centered(const char *str) {
 TQ_Drawable td; td.n=0; td.v=NULL; _tq_flags=0;
 if (!str   )return td; // null string
 if (!str[0])return td; // blank string
 td.v = malloc((strlen(str)+1) * 4 * sizeof(TQ_Vertex)); 
 if (!td.v  )return td; // malloc error
 const float SPACING = 0.05f;
 float x=0;
 for (const char *p=str; *p; p++) {
  int base = *(unsigned char*)p * 4;
  if (*p != ' ') { // skipping spaces is an optimization
   for (int i=0;i<4;i++) {
    td.v[td.n] = _tq_alphabet[base+i];
    td.v[td.n].x += x;
    td.v[td.n].y += 0.5f;
    td.n++;
   }
  } x += _tq_alphabet[base].x + SPACING;
 } x -= SPACING;
 x *= 0.5f;
 for (int i=0; i<td.n; i++) td.v[i].x -= x;
 _tq_flags |= TQ_FLAG_COMPLETE;
 return td;
}


TQ_Drawable tq_centered_fitted(const char *str, float width, float height) { // Nominal font size is 1. Actual font size may vary slightly to fit.
 TQ_Drawable td; td.n=0; td.v=NULL; _tq_flags=0;
 if (!str   )return td; // null string
 if (!str[0])return td; // blank string
 td.v = malloc((strlen(str)+3+1) * 4 * sizeof(TQ_Vertex));
 if (!td.v  )return td; // malloc error
 const float SPACING = 0.05f;
 const char *p = str;
 float       x=0, y=0;
 const char *ls = p; // line start
 const char *le = p; // line end
 float       lw = 0; // line width
 float       widest=0;// widest line
 while (*p && y > -height+0.99f) {
  // determine where the next line should end...
  lw = x = 0;
  while (1) {
   if (*p=='\0'||*p=='\n'){ le=p; lw=x; break; }
   if (isspace(*p))       { le=p; lw=x;        }
   x += _tq_alphabet[*(unsigned char*)p * 4].x + SPACING;
   if (x >= width && lw>0)  {       break; }
   if (*p=='-' || *p==','){ le=p; lw=x;        } // XXX: maybe add more "word-breaking" chars to this list? Maybe '/' and '(' and ')'? What else? Maybe all chars except '.'? I could imagine different contexts in which that might be good or bad. Maybe provide some markup thing similar to <nobr /> in html? But that opens a whole new set of problematic cases. Least invasive way would be with some ascii char value >= 128. But that begs the question of how to consistantly allow a user to type it in a text editor etc
   p++;
  } lw -= SPACING;
  // generate quads of that line...
  if (lw <= width) { // usual case
   x = -0.5f * lw;
   if (widest<lw) widest=lw;
   for (p=ls; p<=le; p++) {
    int base = *(unsigned char*)p * 4;
    if (*p != ' ') { // skipping spaces is an optimization
     for (int i=0;i<4;i++) {
      td.v[td.n] = _tq_alphabet[base+i];
      td.v[td.n].x += x;
      td.v[td.n].y += y;
      td.n++;
     }
    } x += _tq_alphabet[base].x + SPACING;
   } y -= 1.f;
  }
  else { // case where line is one word and too wide: shrink
   x = -0.5f * width;
   widest = width;
   float scale = width/lw;
   float spacing = SPACING*scale;
   for (p=ls; p<=le; p++) {
    int base = *(unsigned char*)p * 4;
    if (*p != ' ') {
     for (int i=0;i<4;i++) {
      td.v[td.n] = _tq_alphabet[base+i];
      td.v[td.n].x *= scale;
      td.v[td.n].x += x;
      td.v[td.n].y *= scale;
      td.v[td.n].y += y;
      td.n++;
     }
    } x += _tq_alphabet[base].x*scale + spacing;
   } y -= scale;
  }
  if (*le == '\0') break;
  ls = p = ++le;
 }
 // expand text if small
 if (widest < width && y > -height) {
  float sx = width/widest;
  float sy = -height/y;
  float scale = sx<sy?sx:sy;
  for (int i=0; i<td.n; i++) {
   td.v[i].x *= scale;
   td.v[i].y *= scale;
  } y *= scale;
 }
 // add ellipsis if text didn't all fit
 if (*le) {
  int base = 4 * (unsigned char)'.';
  for (int h=0;h<3;h++) {
   x = 0.5f*lw + h * (_tq_alphabet[base].x + 0.02f);
   for (int i=0;i<4;i++) {
    td.v[td.n] = _tq_alphabet[base+i];
    td.v[td.n].x += x;
    td.v[td.n].y += y + 1.f;
    td.n++;
   }
  }
 } else _tq_flags |= TQ_FLAG_COMPLETE;
 // center vertically
 y *= 0.5f; for (int i=0; i<td.n; i++) td.v[i].y -= y;
 // done
 return td;
}



void tq_delete(TQ_Drawable *td) {
 free(td->v);
 td->v = NULL;
 td->n = 0;
}



void tq_done() {
 glDeleteTextures(1, &_tq_texture);
}



