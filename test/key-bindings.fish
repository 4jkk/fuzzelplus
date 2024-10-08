#!/usr/bin/fish

# Test overwriting keybindings
echo -e "[key-bindings]\nexecute=y\nexecute=x" >tmp-fuzzel.ini

echo -e "first\nsecond" | ./pkg/fuzzel/usr/bin/fuzzel --dmenu --search second --config tmp-fuzzel.ini &>out.text &
sleep .1
wtype x
sleep .1
set got (cat out.text)

@test "Overriding a default key without unsetting: works" (string match -qe 'second' "$got") $status -eq 0
@test "Overriding a default key without unsetting: warns" (string match -qe 'unmapping default' "$got") $status -eq 0
@test "Overriding user keybinding: warns" (string match -qe 'unmapping keybinding' "$got") $status -eq 0
