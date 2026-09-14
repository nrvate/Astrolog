@AD800  ; Astrolog settings fixture -- every line sets a value switch to a
; sentinel and declares (in its "EXPECT" comment) a regex that must match
; the file saved after loading this one. tools/settings-round-trip.sh leg 3
; extracts and checks them. A switch whose save-twin regresses stops
; matching. Flag switches are covered separately by leg 2's prefix flip.

-n  ; chart for the current moment, so no interactive prompt blocks -od
    ; EXPECT ^-n

; Before "-A", because every "-RA" ends in AdjustAspectCount() and would
; recompute the count from the restrictions it just set.
; The sixteen chart sub-option flags, packed. Bits 0, 2 and 15 set.
-Y2 #8005          ; EXPECT ^-Y2 #8005
-RA1 4 9           ; EXPECT ^-RA1 4 9
-A 11              ; EXPECT ^-A 11
-c Whol            ; EXPECT ^-c Whol
:w 4               ; EXPECT ^:w 4
:I 87              ; EXPECT ^:I 87
-YQ 47             ; EXPECT ^-YQ 47
-Yz 11             ; EXPECT ^-Yz 11
-YP 1              ; EXPECT ^-YP 1
:Xs 300            ; EXPECT ^:Xs 300
:YXg 33            ; EXPECT ^:YXg 33
:YXj 5             ; EXPECT ^:YXj 5
:YXS 2.5           ; EXPECT ^:YXS 2\.5
:YX7 700           ; EXPECT ^:YX7 700
:YXa -5            ; EXPECT ^:YXa -5
-zv 555ft          ; EXPECT ^-zv 555ft
-zj "Fixture Name" "Fixture Place"  ; EXPECT Fixture Name
-YAo 5 5 6.5       ; EXPECT ^-YAo 1 5 .*6\.5
-YAm 5 5 123       ; EXPECT ^-YAm 0 10 .*123
-YAd 5 5 3         ; EXPECT ^-YAd 0 10 +1 1 1 0 0 3
-Yj 5 5 44         ; EXPECT ^-Yj 0 10 +30 30 25 10 10 44
-YjT 5 5 55        ; EXPECT ^-YjT 0 10 +10 10 +4 +8 +9 55
-YkO 5 5 Pink      ; EXPECT ^-YkO 0 10 .*Pin
-YkA 3 3 Sky       ; EXPECT ^-YkA 1 5 +Yel Blu Sky
-YJ Mar Cap 0      ; EXPECT ^-YJ Mar Cap
; And a zero FIRST ruler, which the line above cannot reach: szSignName[0]
; is the empty string, so the writer used to emit "-YJ Ura  0" -- two
; arguments where the switch takes three, and the saved file would not
; load back. Reachable from astrolog.as's own macro 41.
-YJ Ura 0 0        ; EXPECT ^-YJ Ura 0 0
-YJ0 Mar Aqu       ; EXPECT ^-YJ0 Mar Aqu
-Y7O 5 5 7         ; EXPECT ^-Y7O 0 10 +3 +2 4 4 5 7
-Y7C 3 3 45        ; EXPECT ^-Y7C 1 12 +17 +4 45
; Mars restricted here, and Earth unrestricted and Jupiter restricted by
; the "-h Jup" line below, which is what SetCentric() does to the object it
; centers on. Both effects in one sentinel on purpose.
-YR 5 5 1          ; EXPECT ^-YR 0 10 +0 0 0 0 0 1 1
-YRT 5 5 1         ; EXPECT ^-YRT 0 10 +1 0 1 0 0 1
-YjA 4 4 0.7       ; EXPECT ^-YjA 1 5 .*0\.7
-Fm 20 2 3         ; EXPECT ^-Fm 20 2 3
-YD 5 "Ares"       ; EXPECT ^-YD 5 "Ares"
-YAa 5 5 66.6      ; EXPECT ^-Aa 5 66\.6
-YjC 5 5 44        ; EXPECT ^-YjC 1 12 .* 44
-Yk0 3 3 Sky       ; EXPECT ^-Yk0 1 7 +Red Mai Sky
-Yk7 3 3 Sky       ; EXPECT ^-Yk7 1 7 +Red Ind Sky
-Yk 3 3 Sky        ; EXPECT ^-Yk  0 8 +Bla Whi LtG Sky

; ---- T4's other half, 2026-09-02 (work log item 172) ----
; Everything above was chosen by hand. This block was measured: the saved
; file was compared against the EXPECT set, and every value switch the
; writer emits with nothing asserting it got a sentinel here. That is the
; gap item 140 fell through -- the -b family was dropped for five days
; because no fixture line set it.
:d 96                   ; EXPECT ^:d 96
:gs 2                   ; EXPECT ^:gs 2
:pC 2.5                 ; EXPECT ^:pC 2\.5
:pd 360.5               ; EXPECT ^:pd 360\.5
:pO Mar                 ; EXPECT ^:pO Mar
:s Lahi                 ; EXPECT ^:s Lahi
:XS 250                 ; EXPECT ^:XS 250
:Xv 2                   ; EXPECT ^:Xv 2
:Xw 640 480             ; EXPECT ^:Xw 640 480
:XI0 33 1               ; EXPECT ^:XI0 33 1
:Xkv Pur                ; EXPECT ^:Xkv Pur
-Ya1                    ; EXPECT ^-Ya1
-Yi1 "fixdir"           ; EXPECT ^-Yi1 "fixdir"
-Yj0 21 11 16 6         ; EXPECT ^-Yj0 21 11 16 6
-Yj7 11 11 11 6 6 6     ; EXPECT ^-Yj7 11 11 11 6 6 6
-YR0 1 1                ; EXPECT ^-YR0 1 1
-YR1 0 0                ; EXPECT ^-YR1 0 0
-YR2 0 0                ; EXPECT ^-YR2 0 0
-YR7 1 0 0 1 0          ; EXPECT ^-YR7 1 0 0 1 0
-YRp 0 0                ; EXPECT ^-YRp 0 0
-YRZ 1 1 1 1            ; EXPECT ^-YRZ 1 1 1 1
-Yw 3.5                 ; EXPECT ^-Yw 3\.5
:YXp0 9in 12in          ; EXPECT ^:YXp0 9in 12in
:YXv 1 30 12            ; EXPECT ^:YXv 1 30 12
:YXx 3                  ; EXPECT ^:YXx 3
-z0 0                   ; EXPECT ^-z0 0
-zf 77F                 ; EXPECT ^-zf 77F
; -zl's saved form follows the zodiac display format set by :sd below,
; which is worth knowing: a display switch changes how a stored SETTING
; is serialized, so this pattern deliberately stops before the format.
-zl 100W00 50N00        ; EXPECT ^-zl 100
-M0 3 "-i fixture"      ; EXPECT ^-M0 3 "-i fixture"
:an                     ; EXPECT ^:an
; ":an", not "-an": the writer emits the colon form since 2026-09-06,
; because "-a<sort>" also turns the aspect-list CHART on (NSwa opens with
; SwitchF(us.fAspList)) and chart type is not a saved setting. Leg 4
; checks that class directly.
=Yma                    ; EXPECT ^=Yma
; :Xb (bitmap file type) has no fixture line on purpose: NSwXb()
; returns tcError when us.fNoWrite is set (switch.cpp:1474), and
; that is exactly the state a settings save runs in, so the value
; the writer emits cannot be set from here. Measured 2026-09-02.
-Yi2 "fixfont"          ; EXPECT ^-Yi2 "fixfont"
-Yi3 "fixsrc"           ; EXPECT ^-Yi3 "fixsrc"
-YkC Pur Ora Pin Cya    ; EXPECT ^-YkC +Pur Ora Pin Cya
:YXG 212121             ; EXPECT ^:YXG 212121
:Xk Pur                 ; EXPECT ^:Xk Pur
; Five values that reached no line of the save until the writer
; grew one each. Every switch here also toggles a chart type flag,
; which is why the writer spells them with ":".
:L 7 4                  ; EXPECT ^:L 7 4
:Pz 33                  ; EXPECT ^:Pz 33
:N 9                    ; EXPECT ^:N 9
-Yb 11                  ; EXPECT ^-Yb 11
:XL4                    ; EXPECT ^:XL4
-RO Mar                 ; EXPECT ^-RO Mar
:c3 3                   ; EXPECT ^:c3 3
-x 5                    ; EXPECT ^-x 5\.000
-4 3                    ; EXPECT ^-4 3
:Uz                     ; EXPECT ^:Uz
:XU3                    ; EXPECT ^:XU3
-10 Ven                 ; EXPECT ^-10 Ven
:X2 Jup                 ; EXPECT ^:X2 Jup
:Xp                     ; EXPECT ^:Xp
:Xbp                    ; EXPECT ^:Xbp
-h Jup                  ; EXPECT ^-h Jup
:Ys 1.5                 ; EXPECT ^:Ys 1\.50000
-Y1 Mar Ven             ; EXPECT ^-Y1 Mar Ven
-Y5i "fixture-adb"      ; EXPECT ^-Y5i "fixture-adb"
-Y5I 7 3                ; EXPECT ^-Y5I 7 3
:Yao2                   ; EXPECT ^:Yao2
-YkE "fixture-ast"      ; EXPECT ^-YkE "fixture-ast"
-YkU "fixture-starcol"  ; EXPECT ^-YkU "fixture-starcol"
-YRd 5                  ; EXPECT ^-YRd 5
-YRU0 "fixture-stars"   ; EXPECT ^-YRU0 "fixture-stars"
-YUx "fixture-exo"      ; EXPECT ^-YUx "fixture-exo"
-YZ 4                   ; EXPECT ^-YZ 4
-Yz0 42.5               ; EXPECT ^-Yz0 42\.5000
-YzO 1.25               ; EXPECT ^-YzO 1\.2500
-YzC 2.5                ; EXPECT ^-YzC 2\.5000
:v3 2                   ; EXPECT ^:v3 2
:E0 m15                 ; EXPECT ^:E0 m15
:XE1 4 9                ; EXPECT ^:XE1 4 9
:YXj0 7                 ; EXPECT ^:YXj0 7
:YXW 12                 ; EXPECT ^:YXW 12
-YXt "fixture sidebar"  ; EXPECT ^-YXt "fixture sidebar"
-YXU "Sirius" "1"       ; EXPECT ^-YXU "Sirius" "1"
-~Q1 "=a 1"             ; EXPECT ^-~Q1 "=a 1"
; The writer picks the quote character the expression itself does not use.
-~O 'a "quoted" one'    ; EXPECT ^-~O .a .quoted. one.
-zj "Fixture Name" "Fixture Place"  ; EXPECT ^-zj "Fixture Name"
:sd                     ; EXPECT ^:sd
; :YXf has no fixture line either: the writer emits the aggregate
; (":YXf #%06x" of gs.nFontAll, io.cpp) while the switch sets one
; component at a time through a sub-letter -- YXft, YXfs and so on --
; so no single line can set what one line saves. Measured 2026-09-02.

; ---- The last index of every span, 2026-09-13 ----
; Every line above sets one index near the start of its switch's range, so
; a writer that stopped short of the END of a span -- "-YjA" at 18 of 24,
; "-Y7O" skipping 11-33 and 52-83 -- matched every EXPECT here for years.
; Each ranged switch now also sets the last index its registry row accepts
; (fixture_coverage_audit.py checks it). "-YkA" is the one missing, and
; that audit says why.
-YAo 24 24 1.5          ; EXPECT ^-YAo 19 24 .* 1\.5 +; Very
-YAm 84 84 7            ; EXPECT ^-YAm 84 84 +7 +;
-YAd 84 84 2            ; EXPECT ^-YAd 84 84 +2 +;
-Yj 84 84 3             ; EXPECT ^-Yj 84 84 +3 +;
-YjT 84 84 61           ; EXPECT ^-YjT 84 84 +61 +;
-YjC 12 12 7            ; EXPECT ^-YjC 1 12 .* 7 +; Houses
-YjA 24 24 0.35         ; EXPECT ^-YjA 19 24 .* 0\.35 +; Very
; A value of 10 or more, which "%4.1f" wrote with no space before it.
-YjA 20 20 12.5         ; EXPECT ^-YjA 19 24 +0\.05 12\.5 0\.05
-YR 133 133 0           ; EXPECT ^-YR 109 133 .* 1 0 +; Fixed
-YRT 133 133 0          ; EXPECT ^-YRT 109 133 .* 1 0 +; Fixed
-Y7O 83 83 4            ; EXPECT ^-Y7O 52 83 .* 0 4 +; Moons
-Y7O 25 25 7            ; EXPECT ^-Y7O 22 33 +1 4 2 7 1
-Y7C 12 12 3            ; EXPECT ^-Y7C 1 12 .* 5 3 +; Signs
-YkO 84 84 Pink         ; EXPECT ^-YkO 84 84 +Pin
-Yk0 7 7 Sky            ; EXPECT ^-Yk0 1 7 .* Blu Sky +; Rainbow
-Yk7 7 7 Sky            ; EXPECT ^-Yk7 1 7 .* Ros Sky +; Ray
-Yk 8 8 Sky             ; EXPECT ^-Yk  0 8 .* DkB Sky +; Main
-YAa 24 24 170.5        ; EXPECT ^-Aa 24 170\.5

; ---- Settings the writer dropped, 2026-09-13 ----
; Each was accepted and acted on, and lost on the next save. The rulerships
; of any object but the planets, and the rest, are written where they differ
; from the compiled default, which is what these sentinels are.
-YJ Chi Ari Tau         ; EXPECT ^-YJ 11 Ari Tau  ; Chiron
-YJ0 Chi Gem            ; EXPECT ^-YJ0 11 Gem  ; Chiron
-YJ7 Chi Ari 0          ; EXPECT ^-YJ7 11 Ari 0  ; Chiron
-YJ70 Chi Leo 0         ; EXPECT ^-YJ70 11 Leo 0  ; Chiron
-YS Chi 999.125         ; EXPECT ^-YS 11 999\.125
-YE Chi 13.5 .3 0 0 7 0 0 339 0 0 209 1 0 34 713 0.25  ; EXPECT ^-YE 11 13\.5 0\.3 0 0 7 0 0 339 0 0 209 1 0 34 713 0\.25
-YAD 1 "Conjoin" "" ""  ; EXPECT ^-YAD 1 "Conjoin" "" ""
-YI 5 "a fixture mind"  ; EXPECT ^-YI 5 "a fixture mind"
-YIa 3 "fixture curious"      ; EXPECT ^-YIa 3 "fixture curious"
-YIv 4 "fixture desire"       ; EXPECT ^-YIv 4 "fixture desire"
-YIC 6 "fixture area"         ; EXPECT ^-YIC 6 "fixture area"
-YIA 2 "fixture clash"        ; EXPECT ^-YIA 2 "fixture clash"
-YIA0 2 "fixture therefore"   ; EXPECT ^-YIA0 2 "fixture therefore"
