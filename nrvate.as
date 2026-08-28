@AD740  ; Astrolog 7.40 default settings file astrolog.as

-z 8:00W                 ; Default time zone     [hours W or E of UTC   ]
-z0 1                    ; Default Daylight time [0 standard, 1 daylight]
-zl 122:19:59W 47:36:35N ; Default location      [longitude and latitude]
-zv 167ft                ; Default elevation     [in feet or meters     ]
-zj "" "Seattle, WA, USA" ; Default name and location

-n      ; Comment out this line to not start with chart for "now".
-Yz 0   ; Time minute addition to be used if "now" charts are offset.

_s      ; Which zodiac to use       ["_s" is tropical, "=s" is sidereal]
:s 0.0  ; Sidereal zodiac offset    [Change "0.0" to desired ayanamsa  ]
:sz     ; Zodiac display format     ["z" is sign, "d" is 0-360 deg, etc]
-A 9    ; Number of aspects         [Change "5" to desired number      ]
-c Koch ; House system              [Change "Plac" to desired system   ]
_c3     ; 3D house boundaries       ["=c3" is 3D houses, "_c3" is 2D   ]
=k      ; Ansi color text           ["=k" is color, "_k" is monochrome ]
:d 48   ; Searching divisions       [Change "48" to desired divisions  ]
=b0     ; Print zodiac seconds      ["_b0" to minute, "=b0" to second  ]
=b      ; Use ephemeris files       ["=b" uses them, "_b" doesn't      ]
:w 0    ; Wheel chart text rows     [Change "0" to desired wheel rows  ]
:I 80   ; Text screen columns       [Change "80" to desired columns    ]
-YQ 0   ; Text screen scroll limit  [Change "24" or set to "0" for none]
_Yn     ; Which Nodes and Lilith    ["_Yn" shows mean, "=Yn" shows true]
_sr0    ; Latitudes or declinations ["_sr0" shows lat., "=sr0" declin. ]
_Yr     ; Show rounded positions    ["=Yr" rounds, "_Yr" doesn't       ]
_Yd     ; European date format      ["_Yd" is M/D/Y, "=Yd" is D-M-Y    ]
=Yt     ; European time format      ["_Yt" is AM/PM, "=Yt" is 24 hour  ]
_Yv     ; European length units     ["_Yv" is imperial, "=Yv" is metric]
=YC     ; Smart cusp displays       ["=YC" is smart, "_YC" is normal   ]
=YO     ; Smart copy and printing   ["=YO" does it smart, "_YO" doesn't]
_Y8     ; Clip text to end of line  ["=Y8" clips, "_Y8" doesn't clip   ]
-Ya0    ; Input character encoding  [0-3 is Default, IBM, Latin-1, UTF8]
-YP 0   ; Arabic part formula       ["1" is fixed, "0" checks if night ]
=Yu0    ; Show eclipse information  ["=Yu0" shows, "_Yu0" doesn't show ]
=0n     ; Internet Web queries      ["=0n" disables them, "_0n" allows ]

:pd 365.24219 ; Progression degrees per day    [365 is secondary]
:pC 1.0       ; Progressed cusp movement ratio [1.0 is quotidian]


; FILE PATHS (-Yi1 through -Yi9):
; For example, point -Yi1 to ephemeris dir, -Yi2 to chart files dir, etc.

-Yi1 "/swe"
-Yi2 "/swe"
-Yi3 "/swe"


; DEFAULT RESTRICTIONS:
;  0-10: Ear Sun Moo Mer Ven Mar Jup Sat Ura Nep Plu
; 11-21: Chi Cer Pal Jun Ves Nor Sou Lil For Ver EP
; 22-33: Asc 2nd 3rd Nad 5th 6th Des 8th 9th MC 11th 12th
; 34-42: Vul Cup Had Zeu Kro Apo Adm Vulk Pos
; 43-51: Hyg Pho Eri Hau Mak Gon Qua Sed Orc
; 52-83: Planetary moons
; 84-130: Fixed stars

-YR 0 10     1 0 0 0 0 0 0 0 0 0 0    ; Planets
-YR 11 21    0 0 1 1 1 0 0 1 0 1 1    ; Minor planets
-YR 22 33    0 1 1 0 1 1 0 1 1 0 1 1  ; House cusps
-YR 34 42    0 0 1 1 1 1 1 0 0        ; Uranians
-YR 43 51    1 0 0 0 1 1 1 0 0        ; Dwarfs

; DEFAULT TRANSIT RESTRICTIONS:

-YRT 0 10    1 0 0 0 0 0 0 0 0 0 0    ; Planets
-YRT 11 21   0 0 1 1 1 0 0 1 0 1 1    ; Minor planets
-YRT 22 33   1 1 1 1 1 1 1 1 1 1 1 1  ; House cusps
-YRT 34 42   0 0 1 1 1 1 1 0 0        ; Uranians
-YRT 43 51   1 0 0 0 1 1 1 0 0        ; Dwarfs

-YR0 0 0  ; Restrict sign, direction changes
-YR1 1 1  ; Restrict latitude, distance events

-YR7 0 0 1 0 1  ; Restrict rulerships: std, esoteric, hierarch, exalt, ray
-YRZ 0 0 0 0    ; Restrict angle events: rising, zenith, setting, nadir


; DEFAULT ASPECT ORBS:
;  1- 5: Con Opp Squ Tri Sex
;  6-11: Inc SSx SSq Ses Qui BQn
; 12-18: SQn Sep Nov BNv BSp TSp QNv
; 19-24: TDc Un1 Un2 Un3 Un4 Un5

-YAo 1 5     5.0 5.0 5.0 5.0 5.0          ; Major aspects
-YAo 6 11    3.0 2.0 2.0 2.0 1.0 1.0      ; Minor aspects
-YAo 12 18   1.0 1.0 1.0 1.0 1.0 1.0 1.0  ; Obscure aspects
-YAo 19 24   1.0 1.0 1.0 1.0 1.0 1.0  ; Very obscure aspects

; DEFAULT MAX PLANET ASPECT ORBS:

-YAm 0 10    360 360 360 360 360 360 360 360 360 360 360      ; Planets
-YAm 11 21   360 360 360 360 360   2   2   2 360 360   2      ; Minor planets
-YAm 22 33   360 360 360 360 360 360 360 360 360 360 360 360  ; Cusp objects
-YAm 34 42   360 360 360 360 360 360 360 360 360              ; Uranians
-YAm 43 51   360 360 360 360 360 360 360 360 360              ; Dwarfs
-YAm 84 84     2                                              ; Fixed stars

; DEFAULT PLANET ASPECT ORB ADDITIONS:

-YAd 0 10    0 0 0 0 0 0 0 0 0 0 0    ; Planets
-YAd 11 21   0 0 0 0 0 0 0 0 0 0 0    ; Minor planets
-YAd 22 33   0 0 0 0 0 0 0 0 0 0 0 0  ; Cusp objects
-YAd 34 42   0 0 0 0 0 0 0 0 0        ; Uranians
-YAd 43 51   0 0 0 0 0 0 0 0 0        ; Dwarfs
-YAd 84 84   0                        ; Fixed stars


; DEFAULT INFLUENCES:

-Yj 0 10    30 30 25 10 10 10 10 10  8  8  8     ; Planets
-Yj 11 21    6  5  5  5  5  5  5  4  4  4  4     ; Minor planets
-Yj 22 33   20 10 10 10 10 10 10 10 10 15 10 10  ; Cusp objects
-Yj 34 42    4  3  3  3  3  3  3  3  3           ; Uranians
-Yj 43 51    3  3  3  3  3  3  3  3  3           ; Dwarfs
-Yj 84 84    2                                   ; Fixed stars

-YjC 1 12   20 0 0 10 0 0 5 0 0 15 0 0  ; Houses

-YjA 1 5    1.0 0.9 0.8 0.5 0.5          ; Cjn Opp Squ Tri Sex
-YjA 6 11   0.4 0.4 0.6 0.6 0.2 0.2      ; Inc Ssx Ses Ssq Qui Bqn
-YjA 12 18  0.2 0.2 0.1 0.1 0.2 0.2 0.1  ; Sqn Sep Nov Bnv Bsp Tsp Qnv
-YjA 19 24  0.2 0.3 0.3 0.3 0.3 0.1      ; Tdc Un1 Un2 Un3 Un4 Un5
; DEFAULT TRANSIT INFLUENCES:


-YjT 0 10    10 10 4 8 9 20 30 35 40 45 50  ; Planets
-YjT 11 21   30 15 15 15 15 30 30 1 1 1 1   ; Minor planets
-YjT 34 42   50 50 50 50 50 50 50 50 50  ; Uranians
-YjT 43 51  15 30 50 50 50 50 50 50 50        ; Dwarfs
-YjT 84 84  60                                ; Fixed stars

-Yj0 20 10 15 5  ; In ruling sign, exalted sign, ruling house, exalted house
-Yj7 10 10 10 5 5 5  ; In Esoteric, Hierarchical, Ray ruling (signs, houses)


; DEFAULT RAYS:

-Y7C 1 12   17 4 2 37 15 26 3 4 456 137 5 26  ; Signs
-Y7O 0 10   3 2 4 4 5 6 2 3 7 6 1             ; Planets
-Y7O 34 42  1 0 0 0 0 0 0 0 0                 ; Uranians
-Y7O 43 51  0 0 3 2 1 4 7 6 5                 ; Dwarfs


; DEFAULT COLORS:
; Black, White, Gray, LtGray, Red, Orange, Yellow, Green, Cyan, Blue, Purple,
; Magenta, Maroon, DkGreen, DkCyan, DkBlue; Element, Ray, Star, Planet

-YkO 0 10   Yel Ele Ele Ele Yel Ele Ele Ele Ele Ele Ele      ; Planet colors
-YkO 11 21  Mag Mag Mag Mag Mag DkC DkC DkC DkC DkC DkC      ; Minor colors
-YkO 22 33  Ele Ele Ele Ele Ele Ele Ele Ele Ele Ele Ele Ele  ; Cusp colors
-YkO 34 42  Pur Pur Pur Pur Pur Pur Pur Pur Pur              ; Uranian colors
-YkO 43 51  Mag Mag Pur Pur Pur Pur Pur Pur Pur              ; Dwarf colors
-YkO 52 63  Pla Pla Pla Pla Pla Pla Pla Pla Pla Pla Pla Pla  ; Moons
-YkO 64 75  Pla Pla Pla Pla Pla Pla Pla Pla Pla Pla Pla Pla  ; Moons
-YkO 76 83  Pla Pla Pla Pla Pla Pla Pla Pla                  ; Moons
-YkO 84 84  Sta                                              ; Fixed stars

; -YkA 1 18  11 12 9 10 14 13 13 11 11 6 6 6 1 3 3 1 3 3  ; Aspect colors

-YkA 1 1 11    ; Con
-YkA 2 2 12    ; Opp
-YkA 3 3 9     ; Squ
-YkA 4 4 10    ; Tri
-YkA 5 5 14    ; Sex
-YkA 6 6 13    ; Inc
-YkA 7 7 13    ; Ssx
-YkA 8 8 11    ; Ssq
-YkA 9 9 11    ; Ses
-YkA 10 10 6   ; Qui
-YkA 11 11 6   ; Bqn
-YkA 12 12 6   ; Sqn
-YkA 13 13 1   ; Sep
-YkA 14 14 2   ; Nov
-YkA 15 15 2   ; Bnv
-YkA 16 16 1   ; Bsp
-YkA 17 17 1   ; Tsp
-YkA 18 18 2   ; Qnv

-YkA 19 19 6   ; Tdc
-YkA 20 20 3   ; Un1
-YkA 21 21 3   ; Un2
-YkA 22 22 3   ; Un3
-YkA 23 23 3   ; Un4
-YkA 24 24 3   ; Un5



-YkC        Red Yel Gre Cyan                     ; Element colors
-Yk7 1 7    Red Blu Gre Yel Ora Mag Pur          ; Ray colors
-Yk0 1 7    Red Ora Yel Gre Cya Blu Pur          ; Rainbow colors
-Yk  0 8    Bla Whi LtG Gra Mar DkG DkC DkB Mag  ; Main colors

-YXv 1 55  ; Spider web decoration 55%

; OBJECT CUSTOMIZATION:

; [No objects are different from defaults]


; GRAPHICS DEFAULTS:

=Xm              ; Color charts       ["=Xm" is color, "_Xm" is monochrome]
_Xr              ; Reverse background ["_Xr" is black, "=Xr" is white     ]
:Xw 1260 1260      ; Default X and Y resolution (not including sidebar)
:Xs 200          ; Character scale     [100-400]
:XS 150          ; Graphics text scale [100-400]
=XQ              ; Square charts ["=XQ" forces square, "_XQ" allows rectangle]
=Xu              ; Chart border  ["=Xu" shows border, "_Xu" doesn't show     ]
:Xv 1            ; Wheel fill    ["0" for none, "1" for standard, "2" rainbow]
_Xx              ; Thicker lines ["=Xx" is thicker, "_Xx" is thinner         ]
:Xbw             ; Bitmap file type   ["Xbw" is Windows .bmp, "Xbn" is X11   ]
:YXG 11111       ; Glyph selections [Capricorn, Uranus, Pluto, Lilith, Vertex]
:YXg 0           ; Aspect grid cells  ["0" for autodetect  ]
:YXS 0.0         ; Orbit radius in AU ["0.0" for autodetect]
:YXj 0           ; Orbit trail count
:YX7 600         ; Esoteric ray column influence width
:YXf 00000       ; System fonts to use [text, signs, houses, planets, aspects]
:YXp 0           ; PostScript paper orientation ["-1" portrait, "1" landscape]
:YXp0 8.5in 11in ; PostScript paper X and Y sizes

-XQ0 ; Auto-scale glpyhs astrolog 8.0+

=X               ; Graphics chart display ["_X" is text, "=X" is graphics]

; 16H aspects
-Aa Un1 22.5
-Aa Un2 67.5
-Aa Un3 112.5
-Aa Un4 157.5
-Ao Un1 1
-Ao Un2 1
-Ao Un3 1
-Ao Un4 1


-M0 1 "-i /data/med/defplan.dat"
-WM 1 "Default Planets"

-M0 2 "-i /data/med/noplan.dat"
-WM 2 "All Planets Off"

-M0 3 "-i /data/med/deftran.dat"
-WM 3 "Default Transiters"

-M0 4 "-i /data/med/notran.dat"
-WM 4 "All Transiters Off"

; Harmonic hotkeys
-M0 5 "-x 1"
-WM 5 "H1"
-M0 6 "-x 48"
-WM 6 "H48"
-M0 7 "-x 960"
-WM 7 "H960"
-M0 8 "-x 19200"
-WM 8 "H19200"

-M0 9 "-i /data/med/normasp.dat"
-WM 9 "Normal aspects"

-M0 10 "-i /data/med/mpasp.dat"
-WM 10 "Midpoint aspects"

-M0 11 "-i /data/med/yeb1.dat"
-WM 11 "YEBSet1"

-M0 12 "-i /data/med/yeb2.dat"
-WM 12 "YEBSet2"

-M0 13 "-i /data/med/anglemps.dat"
-WM 13 "AngleMps"


-WN 25 ; 25ms animation delay
-ma ; Show aspects to midpoints
_m

-ao  ; sort aspects by orb

-YXa -3  ; Dash weak aspects

-Yn  ; Use True Lunar Nodes

-YC  ; Don't hide aspects to south node, lower angles, etc

~ma "Lt Abs @z 1.0"  ; Midpoint aspects restricted to 1 degree

-Fm 19 1 2  ; POF = Sun/Moon midpoint

-v3 ; Decans on wheel

-Xv 0 ; Annoying wheel fill off

; astrolog.as
-Yeb 34 7066 -YD 34 "Nessus"
; -Yeb 42 120347 -YD 42 "Salacia"
; -Yeb 35 10199 -YD 35 "Chariklo"
; -Yeb Vulk 463368 -YD Vulk "Eurytus"
-Am 34 1.5
-Am 35 1.5
-Am 36 1.5
-Am 37 1.5
-Am 38 1.5
-Am 39 1.5
-Am 40 1.5
-Am 41 1.5
-Am 42 1.5
-Am 43 1.5
-Am 44 1.5
-Am 45 1.5
-Am 46 1.5
-Am 47 1.5
-Am 48 1.5
-Am 49 1.5
-Am 50 1.5
-Am 51 1.5
