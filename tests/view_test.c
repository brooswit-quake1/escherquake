#include <assert.h>
#include <stdio.h>
#include "../vendor/fteqw/engine/client/escher_view.h"
static float dot(const float *a, const float *b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
int main(void)
{
 float down[3]={0,0,-1}, wall[3]={-1,0,0}, ceiling[3]={0,0,1};
 float gaze[3]={0.8f,0,0.6f}, up[3]={0,0,1}, before, next[3];
 int i;
 before=dot(gaze,down);
 Escher_ViewTransport(down,wall,gaze);
 assert(fabsf(dot(gaze,wall)-before)<0.00001f);
 assert(fabsf(dot(gaze,gaze)-1)<0.00001f);
 Escher_ViewTransport(wall,down,gaze);
 assert(fabsf(gaze[0]-0.8f)<0.00001f && fabsf(gaze[2]-0.6f)<0.00001f);
 Escher_ViewTransport(down,ceiling,up);
 assert(fabsf(up[2]+1)<0.00001f);
 /* Thousands of tiny frame changes must preserve local pitch and unit length. */
 for(i=1;i<=10000;i++)
 {
  float a=i*0.0003f;
  next[0]=-sinf(a); next[1]=0; next[2]=-cosf(a);
  Escher_ViewTransport(down,next,gaze);
  down[0]=next[0]; down[2]=next[2];
 }
 assert(fabsf(dot(gaze,down)-before)<0.001f);
 assert(fabsf(dot(gaze,gaze)-1)<0.001f);
 puts("camera transport tests passed");
 return 0;
}
