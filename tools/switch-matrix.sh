#!/bin/sh
# The switch-surface behavior matrix: 529 invocations covering every
# registry family -- valid, edge, names-as-indexes, and error shapes --
# printing each run's stderr plus the relevant lines of the settings
# file it saves, normalized so two binaries can be byte-diffed.
#
# This is the harness that proved every step of the M1-M10 parser
# migration (REFACTORING.md, T3) behavior-preserving. To verify a
# change to switch parsing or the registry:
#
# The per-run window was "head -30" until 2026-08-31, while the filter
# it caps selects 159 lines out of a settings dump -- so this gate, the
# one every parser increment was proven against, was comparing 19% of
# the surface it had already chosen, and any change past the thirtieth
# matching line was invisible to it. The cap now clears every dump with
# headroom (1000 is above any dump: a whole settings file is ~330
# lines). Found by a change that ADDED five settings lines: the five
# pushed five others off the bottom, which is what a fixed window does
# to a diff that is supposed to be exact.
#
#   git worktree add /tmp/base <commit>   # the pre-change baseline
#   make -C /tmp/base -j4                 # (never stash around this;
#   cp /tmp/base/astrolog ./base-astrolog #  see the work log)
#   git worktree remove /tmp/base --force
#   tools/switch-matrix.sh ./base-astrolog > old.txt 2>&1
#   tools/switch-matrix.sh ./astrolog     > new.txt 2>&1
#   diff old.txt new.txt                  # empty = proven
#
# Both binaries must sit at equally short paths (a deep path used to
# crash startup; item 68) and run from the repo root so astrolog.as
# and the ephemeris resolve identically. Charts of "now" never reach
# the output -- only stderr and saved settings -- so runs minutes
# apart still compare byte-for-byte.
B=$1; T=$(mktemp -d)
run() {
  echo "== $*"
  # "timeout 60" is not decoration. This was the only one of the four
  # matrices without it, and on 2026-09-02 that cost 24 minutes of a CI
  # run: the chart matrix finished in 26 seconds and this one was still
  # going when the job was cancelled, with the runner naming
  # switch-matrix.sh as the orphan it had to kill. 529 invocations of the
  # whole switch surface, on a machine with no /swe, no display and no
  # window manager, is exactly where an invocation nobody anticipated
  # blocks -- and without a bound, one of them stops the harness rather
  # than failing in it. Both binaries get the same bound, so a timeout
  # that happens on both sides still diffs to zero.
  env -u DISPLAY timeout 60 $B -n "$@" _X -od $T/o.as </dev/null 2>&1 >/dev/null | sed "s|$T|TMP|g" | head -2
  grep -E "^-YA[oamd]|^-Yj|^-YJ|^-Yk|^-YAa|^-YR|^-Y7|^-YD|^-Ye|^-zl|^-z0|^:YX|^[=_]YX|^:Xw|^:Xs|^:XS|^[=_]X|^:XE|^:X1|^-A |^[=_]?RO?|^[=_]?[bspc1-4fGJ9]|^-h|^-x|^-F|^:p|^-z|^-M0|^:5|^[=_]k" $T/o.as 2>/dev/null | head -1000
  rm -f $T/o.as
}
run -Yj 5 5 44
run -Yj Mar Jup 41 42
run -YjT 0 2 91 92 93
run -YjC 1 12 1 2 3 4 5 6 7 8 9 10 11 12
run -YjA 0 3 9 8 7 6
run -YjA 1 3 9 8 7
run -Yj0 11 12 13 14
run -Yj7 21 22 23 24 25 26
run -YAo 1 5 6.5 6.5 6.5 6.5 6.5
run -YAa 19 24 31 32 33 34 35 36
run -YAm 84 84 55
run -YAd 0 3 1.5 1.5 1.5 1.5
run -YAD 6 Inconjunct Inc x
run -YJ Mar Cap Sco
run -YJ Sun 0 Leo
run -YJ0 Ven Pis
run -YJ7 Mon Ari 0
run -YJ70 Plu Ari Tau
run -Yj 900 901 1 1
run -Yj 5 4 1
run -YAo 0 5 1 1 1 1 1 1
run -YAo 5
run -YJ Mar 44 0
run -YJ zzz 1 0
run -YAD 99 a b c
run -YjT 84 84
run -YR 5 8 1 1 0 1
run -YRT 0 2 1 0 1
run -YR 84 86 1 0 1
run -YR Mar Jup 1 0 1
run -YR0 1 0
run -YR1 0 1
run -YR2 1 1
run -YRp 1 0
run -YRZ 1 0 1 0
run -YR7 0 1 1 0 1
run -YRd 3
run -YRh
run -YRo
run -YRi
run -YRU "Sirius Vega"
run -YRU0 "Sirius"
run -Y7O 5 7 3 4 5
run -Y7O Mar Mar 7
run -Y7C 1 3 12 34 567
run -YkO 5 7 Red Gre Blu
run -YkA 1 3 Mag Cya Yel
run -Yk0 1 3 Red Ora Yel
run -Yk7 1 2 Ind For
run -Yk 0 2 Bla Whi LtG
run -YkC Red Yel Gre Blu
run -YkU Whi
run -YkE Mag
run -YR 900 901 1 1
run -YR 5 4 1
run -Y7O 5 5 9
run -Y7C 2 2 8
run -YkA 1 1 zzz
run -Yk0 0 3 Red Ora Yel Gre
run -YRZ 1 0
run -Y7C Mar Ven 1
run -YD 5 "Ares"
run -YD 5 x
run -YS 5 6800km
run -YS Mar 4212mi
run -YU 85 "TestStar"
run -YUb
run -YUb0
run -YUx "Kepler-22b"
run -YF 19 15 Sco 30 5 10 0.5 1.2
run -Ye 34 5000
run -Yeb 35 -1
run -YeO 36 Mar
run -Yem 37 502
run -Yej 38 5
run -YeA 39 3
run -Yemn 40 501
run -YemsHS 41 502
run -YE 5 1.5 0.1 0 0 1 0 0 2 0 0 3 0 0 4 0 0 5
run -YI 5 "warrior spirit"
run -YIa 3 "curious"
run -YIv 3 "communicates"
run -YIC 7 "partnerships"
run -YIA 3 "clashes with"
run -YIA0 3 "Tension results"
run -YYt "hello"
run -YYT "modal hello"
run -zL "Seattle, WA"
run -zN "Seattle, WA"
run -YY 1
run -YD 999 x
run -YS 5 -3
run -YU 5 x
run -YF 5
run -Ye 5 100
run -YI 999 x
run =YT
run _YT
run =YV
run =Yf
run =Yh
run =Ym
run =Yn0
run =Ynn
run _Yn
run =Yd
run =Yt
run =Yv
run =Yr
run =YC
run =YO
run =Y8
run =Yo
run =Yc
run =Yp
run =Y0
run =Yz1
run =Yu
run _Yu
run =Yu0
run _Yu0
run =Ys
run =Ys 25.3
run -Yw 1.5
run -YQ 33
run -Yz 11
run -Yz0 66.6
run _Yz0
run -YzO 0.25
run -YzC 0.5
run -Y1 5 6
run -Y10 5 6
run _Y10 5 6
run -YZ 3
run -Yl 7
run -Yb 27
run -YP 1
run -YB
run -Ya1
run -Yao2
run -Ya
run -Yq2 "-a" "-b"
# Work log item 145: -YYt took its argument straight into a 1020-byte
# buffer through a loop with no end check, so a long one smashed the stack
# and cored the release build -- shared core, so the Windows build had it
# too. A crash shows up as this run's stderr changing, which is what this
# harness is for. The sibling defect in the graphics sidebar (-YXt) is NOT
# here on purpose: this harness never renders, so that switch only stores a
# string and the crash is unreachable. Measured, not assumed -- adding it
# produced no diff at all. Its net is the 3000-character sidebar render in
# qttest.cpp's shared-core group, which aborts if that bound is removed.
LONGARG=`awk 'BEGIN{s="";while(length(s)<3000)s=s "Y";print s}'`
run -YYt "$LONGARG"
run -Yi5 "some/path"
run -Y5
run -Y52
run -Y5i "adbfile"
run -Y5I 3 7
run -YXg 24
run -Yw -1
run -YQ -2
run -YZ 9
run -Yl 99
run -Yb 99
run -YP 5
run -Y1 999 5
run -Yq3 "-a"
run -YX 1 2
run -YXG 122111
run -YXGc 2
run -YXGp 3
run -YXGe 2
run -YXD 5 "xx" "yy"
run -YXD1 5 "zz"
run -YXDD 5 6
run -YXA 3 "aa" "bb"
run -YXA1 3 "cc"
run -YXv 2
run -YXv 2 40
run -YXv 2 40 3
run -YXt "sidebar text"
run -YXS 5.5
run -YXj 12
run -YXj0 7
run -YX7 550
run =YXk
run =YXk0
run =YXe
run -YXK 5 255
run =YXK0
run -YXa -4
run -YXx 2
run -YXW 3
run -YXf 111111
run -YXft 1
run -YXfo 1
run -YXp 1
run -YXp0 8.5in 11in
run -YXG 999999
run -YXGc 5
run -YXD 999 a b
run -YXS -1
run -YXf 99999999
run =X
run -Xb
run -XbB
run -XbW
run -XbP
run -Xp
run -Xp0
run -XM2 "w1" "w2"
run -XM10 "w0"
run -XM
run -XV
run -X3
run -Xo outfile.bmp
run -XI0 50 1
run _XI0
run =Xm
run -Xr
run -Xw 700
run -Xw 700 500
run -Xs 250
run -XS 150
run =XQ
run =XQ0
run =Xi
run =Xt
run =Xu
run =Xx
run =Xx0
run =Xl
run =XA
run =Xj
run =Xe
run =XU
run =XU2
run =XUx
run -XE1 100 200
run =XL3
run =XC
run -X1 5
run -X2 6
run _X1
run -Xv 2
run =Xv0
run =XJ
run =X8
run -XX 30 40
run -XX0
run -XW 25
run -XW0
run -XG 10 20
run -XP 15
run -XPv
run -XZ Mar
run _XZ Mar
run -XF
run -Xk Red
run -Xkv Blu
run -Xn 5
run _Xn 5
run =Xnp
run -Xnf 3
run =XN
run -Xw 99
run -Xs 999
run -Xk zzz
run -XE1 100
run -Xnf 99
run -R
run =R
run -R Sun Moon
run _R Mar
run -R0
run -R1
run -R2
run -RC
run -Ru
run -Ru0
run -R8
run -Rb
run -RU
run -RC 22 25
run -RT
run -RT0
run -RT1
run -RT2
run -RTC
run -RTu
run -RTu0
run -RT Ven Jup
run -RA 6 7
run -RA0
run -RA1
run -RA0 1 3 5
run -RO Mar
run -RO -1
run -C
run =C
run -u
run -u0
run -u8
run -ub
run =U
run -Uz
run -Ul
run -Un
run -Ux
run -Uxd
run -Uxm
run -UxY
run -A 12
run -A3
run -Ap
run -AP
run -Ao 5 6.5
run -Am 5 123
run -Ad 5 2.5
run -Aa 5 72.5
run -A 99
run -RO 999
run -Ao 5
run -Am 99 5
run -R 999
run =b
run _b
run =b0
run =b1
run =b2
run =bs
run =bj
run =bp
run =bm
run =ba
run =bU
run -c Koch
run -c 5
run -c3
run -c3 2
run -c3 0
run -c zzz
run =s
run _s
run =s 25.5
run =sr
run =sr0
run =sz
run =sh
run =sd
run =sn
run -h
run -h Sun
run -h Mar
run -p
run _p
run =p0
run =p1
run -pd 365.25
run -pd X360
run -pC 2
run -pO Sun
run =pc
run -p 8 15 2020
run -pt 8 15 2020 12:30
run -x 2
run -x D180
run -x 999
run -1
run -1 Mar
run -10 Jup
run _1
run -2 Ven
run -20 Sat
run =3
run -4
run -4 2
run _4
run =f
run =G
run =J
run =9
run -F 19 Sco 15.5
run -Fm 19 1 2
run _F 19
run -F 999 Sco 5
run -Fm 19 999 2
run -p 99 15 2020
run -v
run =v0
run -v3
run -v3 2
run -w
run -w 4
run -w0 3
run -g
run -g0
run -gm
run -gp
run -gd
run -ga
run -gs 2
run -a
run -a0
run -a0s 3
run -ap
run -aj
run -aO
run -m
run -m0
run -ma
run -Z
run -Z0
run -Zd
run -Zdy
run -ZdY 3
run -S
run -l
run -l0
run -j
run -j0
run -7
run -L
run -L 5
run -L0 10 100
run -K
run -Ky
run -d
run -d 12
run -dm
run -dy
run -dY 2
run -dp 8 2020
run -dpy 2020
run -dp0 8 2020
run -D
run -E
run -Ey
run -EY 3
run -8
run -80
run -e
run -t 8 2020
run -ty 2020
run -td 8 15 2020
run -tp 8 2020
run -tr 8 2020
run -T 8 15 2020
run -Tt 8 15 2020 12:00
run -Tn
run -B
run -Bm
run -Bp
run -B0
run -V 8 15 2020
run -Vm 8 2020
run -Vy 2020
run -P
run -P 30
run -Pz
run -P0
run -N
run -Nz
run -Nl
run -I
run -I 100
run -I0
run -t 99 2020
run -V 99 15 2020
run -w 99
run -L 99
run -H
run -Hc
run -HC
run -HO
run -HA
run -HF
run -HS
run -H7
run -HI
run -HY
run -M0 5 "-A 7"
run -M0 5 "-A 7" -M 5
run -M1 "wheelfile"
run -zt 14:30
run -zd 10
run -zm 3
run -zy 1999
run -zi "Someone" "Somewhere"
run -zZ 5:00W 1
run -z 6:00W
run -z
run -q 8 15 2020
run -qy 2020
run -qm 3 2020
run -qd 3 10 2020
run -qa 3 10 2020 10:30 8:00W 122W 47N
run -qb 3 10 2020 10:30 1 8:00W 122W 47N
run -qj 2459000.5
run -q1 8 15 2020
run -i nofilehere.as
run -ix
run -o outtest.as
run -o outtest.as comment1 comment2
run -os screentest.txt
run +
run -
run + 3
run - 3
run +y
run +m 2
run --t 5
run -r one.as two.as
run _r
run -r1
run -rP 2
run -y other.as
run -5
run -5d
run -50
run -5f "1" "x"
run -kh
run -k
run -k0
run -k1
run -WN 100
run -Wh
run =0
run _0
run -0q
run -0on
run "-;" -A 9
run -~ "Add 1 2"
run -~g "True"
run -~M 2 "-A 8"
run -~2 1 "hello"
run -M 99
run -q 99 15 2020
run -r one.as
run -0z
rm -f outtest.as screentest.txt
rmdir $T 2>/dev/null
