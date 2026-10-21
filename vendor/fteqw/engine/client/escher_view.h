/* EscherQuake, GPL-2.0-or-later. Standalone so the actual camera math is testable. */
#ifndef ESCHER_VIEW_H
#define ESCHER_VIEW_H
#include <math.h>
static void Escher_ViewTransport(const float olddown[3], const float newdown[3], float v[3])
{
 double axis[3], cross[3], len, c, s, along, result[3], norm;
 int i;
 c = (double)olddown[0]*newdown[0] + (double)olddown[1]*newdown[1] + (double)olddown[2]*newdown[2];
 if (c > 1) c = 1;
 if (c < -1) c = -1;
 axis[0] = olddown[1]*newdown[2]-olddown[2]*newdown[1];
 axis[1] = olddown[2]*newdown[0]-olddown[0]*newdown[2];
 axis[2] = olddown[0]*newdown[1]-olddown[1]*newdown[0];
 len = sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
 s = len;
 norm = sqrt(c*c+s*s);
 if (norm == 0) return;
 c /= norm; s /= norm;
 if (len < 0.000001f)
 {
  if (c > 0) return;
  s = 0; c = -1;
  /* Match the deterministic antipodal axis in escher.qc. */
  if (fabs(olddown[0]) < 0.9f)
  { axis[0]=0; axis[1]=olddown[2]; axis[2]=-olddown[1]; }
  else
  { axis[0]=-olddown[2]; axis[1]=0; axis[2]=olddown[0]; }
  len = sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
 }
 for (i=0; i<3; i++) axis[i] /= len;
 along = axis[0]*v[0]+axis[1]*v[1]+axis[2]*v[2];
 cross[0]=axis[1]*v[2]-axis[2]*v[1];
 cross[1]=axis[2]*v[0]-axis[0]*v[2];
 cross[2]=axis[0]*v[1]-axis[1]*v[0];
 for (i=0; i<3; i++) result[i]=v[i]*c+cross[i]*s+axis[i]*along*(1-c);
 for (i=0; i<3; i++) v[i]=result[i];
}
#endif
