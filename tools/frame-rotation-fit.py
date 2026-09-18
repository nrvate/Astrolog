"""tools/frame-rotation-fit.py -- is a frame rotation DERIVABLE from the bodies?

Run by hand, on the output of a sampling run that asks a live astrolog-ephd
for several bodies AND several fixed stars in two frames (see
tools/node-frame-probe.sh for that shape).

It exists to settle one question in EPHEMERIS_ACCURACY_REGISTRY.md 4.2.
Fixing the node in a fixed frame means rotating the of-date node into that
frame, and Swiss exports no precession routine -- swe_cotrans is a
single-axis obliquity rotation and that is all. But the rotation does not
have to be REPRODUCED; it can be DERIVED, because three independent
directions in both frames determine it and Swiss will give any body in both
frames. The node then rotates by literally the same rotation the bodies got.

The objection is conditioning: every body is near the ecliptic, so does a
rotation fitted from them hold off it? This fits from the BODIES ONLY and
then predicts the STARS, which is the out-of-sample test. Measured at three
epochs, from bodies confined to ecliptic latitudes -1.7..+1.1 degrees:
Polaris at +66 degrees predicted to 5e-11 arcsec, Vega at +62 to 9e-11.
That is the float64 noise floor, so near-coplanarity only had to leave the
fit non-degenerate, not well-spread -- a rotation has three parameters and
five directions over-determine it.

The residual is also the health guard a production version would want: a
degenerate instant announces itself rather than returning a quiet wrong
matrix.
"""
import sys
import math,collections
D=math.pi/180
d=collections.defaultdict(dict)
for ln in open(sys.argv[1] if len(sys.argv) > 1 else '/dev/stdin'):
    f=ln.split()
    if len(f)!=6 or not f[4].lstrip('-').startswith('0x'): continue
    d[(f[1],f[2])][f[3]]=(float.fromhex(f[4]),float.fromhex(f[5]))
def vec(l,b):
    l*=D;b*=D
    return [math.cos(b)*math.cos(l),math.cos(b)*math.sin(l),math.sin(b)]
def model(p,l0,b0):
    i,om=p[0]*D,p[1]*D
    v=vec(l0,b0)
    x= v[0]*math.cos(om)+v[1]*math.sin(om)
    y=-v[0]*math.sin(om)+v[1]*math.cos(om)
    y2= y*math.cos(i)+v[2]*math.sin(i)
    z2=-y*math.sin(i)+v[2]*math.cos(i)
    return math.atan2(y2,x)/D+p[2], math.asin(max(-1,min(1,z2)))/D
def lstsq(A,b):
    n=len(A[0])
    M=[[sum(A[k][i]*A[k][j] for k in range(len(A))) for j in range(n)]+
       [sum(A[k][i]*b[k] for k in range(len(A)))] for i in range(n)]
    for c in range(n):
        piv=max(range(c,n),key=lambda r:abs(M[r][c])); M[c],M[piv]=M[piv],M[c]
        for r in range(n):
            if r!=c and M[c][c]:
                fq=M[r][c]/M[c][c]
                for k in range(c,n+1): M[r][k]-=fq*M[c][k]
    return [(M[i][n]/M[i][i] if abs(M[i][i])>1e-30 else 0.0) for i in range(n)]
def sep(a,b):
    l1,b1,l2,b2=a[0]*D,a[1]*D,b[0]*D,b[1]*D
    dl=l2-l1
    x=math.sqrt((math.cos(b2)*math.sin(dl))**2+(math.cos(b1)*math.sin(b2)-math.sin(b1)*math.cos(b2)*math.cos(dl))**2)
    y=math.sin(b1)*math.sin(b2)+math.cos(b1)*math.cos(b2)*math.cos(dl)
    return math.atan2(x,y)/D*3600
for jd in sorted({k[0] for k in d}):
    A=d[(jd,'mod')]; B=d[(jd,'j2000')]
    bods=[k for k in A if k.startswith('b') and k in B]
    stars=[k for k in A if not k.startswith('b') and k in B]
    rows=[(A[k][0],A[k][1],B[k][0],B[k][1]) for k in bods]
    def resid(p):
        r=[]
        for l0,b0,l2,b2 in rows:
            L,Bt=model(p,l0,b0); r+=[((L-l2+180)%360-180)*3600,(Bt-b2)*3600]
        return r
    p=[0.0,0.0,0.0]
    for _ in range(80):
        r=resid(p); J=[[0.0]*3 for _ in r]
        for j in range(3):
            q=list(p); q[j]+=1e-7; rq=resid(q)
            for m in range(len(r)): J[m][j]=(rq[m]-r[m])/1e-7
        dd=lstsq(J,[-x for x in r])
        for j in range(3): p[j]+=dd[j]
        if max(abs(x) for x in dd)<1e-13: break
    r=resid(p)
    lats=[A[k][1] for k in bods]
    print("jd %s  fitted from %d bodies (ecliptic latitudes %+.1f..%+.1f deg), in-sample rms %.3e\""
          %(jd,len(bods),min(lats),max(lats),math.sqrt(sum(x*x for x in r)/len(r))))
    for k in sorted(stars):
        pred=model(p,A[k][0],A[k][1])
        print("    OUT OF SAMPLE  %-10s lat %+7.2f deg   predicted vs actual: %.3e\""
              %(k,A[k][1],sep((pred[0]%360,pred[1]),B[k])))
