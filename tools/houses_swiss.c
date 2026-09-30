#include <stdio.h>
#include <stdlib.h>
#include "swephexp.h"
/* tools/houses_swiss.c -- the fork's swe_houses_armc_ex2 for each system: 12 cusps, then Asc MC Vertex EquAsc. For tools/houses_ref.py --compare. */
int main(int argc,char**argv){
  double armc=atof(argv[1]), lat=atof(argv[2]), eps=atof(argv[3]);
  double c[37],a[10],cs[37],as[10]; char e[256]; const char *sys="PKORCBAWXMT";
  for(const char*s=sys;*s;s++){ e[0]=0; int r=swe_houses_armc_ex2(armc,lat,eps,*s,c,a,cs,as,e);
    printf("%c %d",*s,r); for(int i=1;i<=12;i++) printf(" %.9f",c[i]); printf(" | %.9f %.9f %.9f %.9f\n",a[0],a[1],a[3],a[4]); }
}
