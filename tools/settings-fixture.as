@AD800  ; Astrolog settings fixture -- every line sets a value switch to a
; sentinel and declares (in its "EXPECT" comment) a regex that must match
; the file saved after loading this one. tools/settings-round-trip.sh leg 3
; extracts and checks them. A switch whose save-twin regresses stops
; matching. Flag switches are covered separately by leg 2's prefix flip.

-n  ; chart for the current moment, so no interactive prompt blocks -od

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
-YJ0 Mar Aqu       ; EXPECT ^-YJ0 Mar Aqu
-Y7O 5 5 7         ; EXPECT ^-Y7O 0 10 +3 +2 4 4 5 7
-Y7C 3 3 45        ; EXPECT ^-Y7C 1 12 +17 +4 45
-YR 5 5 1          ; EXPECT ^-YR 0 10 +1 0 0 0 0 1
-YRT 5 5 1         ; EXPECT ^-YRT 0 10 +1 0 1 0 0 1
-YjA 4 4 0.7       ; EXPECT ^-YjA 1 5 .*0\.7
-Fm 20 2 3         ; EXPECT ^-Fm 20 2 3
-YD 5 "Ares"       ; EXPECT ^-YD 5 "Ares"
-YAa 5 5 66.6      ; EXPECT ^-Aa 5 66\.6
-YjC 5 5 44        ; EXPECT ^-YjC 1 12 .* 44
-Yk0 3 3 Sky       ; EXPECT ^-Yk0 1 7 +Red Mai Sky
-Yk7 3 3 Sky       ; EXPECT ^-Yk7 1 7 +Red Ind Sky
-Yk 3 3 Sky        ; EXPECT ^-Yk  0 8 +Bla Whi LtG Sky
