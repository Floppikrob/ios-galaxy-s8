#include <math.h>
#include <assert.h>
#include "poster-preview-bounds.h"
int main(void){
 assert(dream_poster_preview_bounds(0,0,375,812,3));
 assert(dream_poster_preview_bounds(0,0,1,1,1));
 assert(!dream_poster_preview_bounds(0,0,0,812,3));
 assert(!dream_poster_preview_bounds(0,0,-1,812,3));
 assert(!dream_poster_preview_bounds(0,0,375,812,0));
 assert(!dream_poster_preview_bounds(0,0,375,812,5));
 assert(!dream_poster_preview_bounds(0,0,8192,8192,1));
 assert(!dream_poster_preview_bounds(NAN,0,375,812,3));
 assert(!dream_poster_preview_bounds(0,0,INFINITY,812,3));
 assert(!dream_poster_preview_bounds(0,0,1e308,812,3));
}
