"""tools/node-frame-fit.py -- the fit behind tools/node-frame-probe.sh.

Reads that script's output (or a file named as argv[1]), fits the three
Euler angles of the date-to-J2000 rotation from the BODY rows alone, then
applies it to the node row and prints what the server answered beside what
the rotation requires. See EPHEMERIS_ACCURACY_REGISTRY.md 4.2.
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
jds=sorted({k[0] for k in d})
for jd in jds:
    A=d[(jd,'mod')]; B=d[(jd,'j2000')]
    bodies=[k for k in A if k!='node' and k in B]
    rows=[(A[k][0],A[k][1],B[k][0],B[k][1]) for k in bodies]
    def resid(p):
        r=[]
        for l0,b0,l2,b2 in rows:
            L,Bt=model(p,l0,b0)
            r+=[((L-l2+180)%360-180)*3600,(Bt-b2)*3600]
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
    r=resid(p); rms=math.sqrt(sum(x*x for x in r)/len(r))
    print("jd %s  rotation fitted from %d BODIES: rms %.5f\"  (tilt %.6f deg)"%(jd,len(rows),rms,p[0]))
    n0=A['node']; n2=B['node']
    pred=model(p,n0[0],n0[1])
    print("   node of date                       lon %11.6f  lat %+9.4f\""%(n0[0],n0[1]*3600))
    print("   that node ROTATED into J2000       lon %11.6f  lat %+9.4f\""%(pred[0]%360,pred[1]*3600))
    print("   what the server ANSWERS in J2000   lon %11.6f  lat %+9.4f\""%(n2[0],n2[1]*3600))
    print("   server vs rotated: %.3f\"   |   server vs 'lat kept 0': %.4f\"\n"%(
        sep((pred[0]%360,pred[1]),n2), abs(n2[1])*3600))
