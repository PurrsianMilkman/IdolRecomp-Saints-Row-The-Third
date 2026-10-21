# Differential test vectors - table XML document parser (Team D, original game as oracle)

Companion data for `spec-differential-results.md` (round 4, D-16). Each input document was parsed by the original executable's in-memory table parser (emulated) and by the rebuilt parser (snapshot 7436a78). Trees are written `name='text'{children}`; no `=` means the element has no text. Byte strings use Python escapes.

| input document | original | Team B |
|---|---|---|
| `<root><Table><A>1</A></Table></root>` | root{Table{A='1'}} | same |
| `<root><Table><A>1</A><B>2</B><A>3</A></Table></root>` | root{Table{A='1', B='2', A='3'}} | same |
| `<root/>` | root | same |
| `<root></root>` | root | same |
| `<root> </root>` | root | root=' ' |
| `<root>text</root>` | root='text' | same |
| `<root><Table><A></A></Table></root>` | root{Table{A}} | same |
| `<root><Table><A/></Table></root>` | root{Table{A}} | same |
| `<root><Table><A /></Table></root>` | root{Table{A}} | same |
| `<root><Table><A> </A></Table></root>` | root{Table{A}} | root{Table{A=' '}} |
| `<root><Table><A>  5  </A></Table></root>` | root{Table{A='5'}} | root{Table{A='  5  '}} |
| `<root><Table><A>\r\n</A></Table></root>` | root{Table{A}} | root{Table{A='\r\n'}} |
| `<root><Table><A>\t1\t</A></Table></root>` | root{Table{A='1'}} | root{Table{A='\t1\t'}} |
| `<root><Table><B>x<C>2</C>y</B></Table></root>` | root{Table{B='x'{C='2'}}} | root{Table{B='xy'{C='2'}}} |
| `<root><Table><B>x<C>2</C></B></Table></root>` | root{Table{B='x'{C='2'}}} | same |
| `<root><Table><B><C>2</C>y</B></Table></root>` | root{Table{B='y'{C='2'}}} | same |
| `<root><Table><B>x<C>2</C>y<D>3</D>z</B></Table></root>` | root{Table{B='x'{C='2', D='3'}}} | root{Table{B='xyz'{C='2', D='3'}}} |
| `<root><Table><B>  x  <C>2</C>  </B></Table></root>` | root{Table{B='x'{C='2'}}} | root{Table{B='  x  '{C='2'}}} |
| `<root><Table><B>\r\n  <C>2</C>\r\n</B></Table></root>` | root{Table{B{C='2'}}} | same |
| `<root><Table><B>0.98 0.98<R>255</R></B></Table></root>` | root{Table{B='0.98 0.98'{R='255'}}} | same |
| `<root><Table><A>&lt;</A></Table></root>` | root{Table{A='<'}} | same |
| `<root><Table><A>&gt;</A></Table></root>` | root{Table{A='>'}} | same |
| `<root><Table><A>&amp;</A></Table></root>` | root{Table{A='&'}} | same |
| `<root><Table><A>&quot;&apos;</A></Table></root>` | root{Table{A='"\''}} | same |
| `<root><Table><A>&#65;&#x42;</A></Table></root>` | whole file rejected | root{Table{A='AB'}} |
| `<root><Table><A>a&b</A></Table></root>` | whole file rejected | root{Table{A='a&b'}} |
| `<root><Table><A>&unknown;</A></Table></root>` | whole file rejected | root{Table{A='&unknown;'}} |
| `<root><Table><A>&lt</A></Table></root>` | whole file rejected | root{Table{A='&lt'}} |
| `<root><Table><A><![CDATA[x<y]]></A></Table></root>` | whole file rejected | root{Table{A='x<y'}} |
| `<root><Table><A>1<!-- c -->2</A></Table></root>` | root{Table{A='1'}} | root{Table{A='12'}} |
| `<root><Table><!-- c --><A>1</A></Table></root>` | root{Table{A='1'}} | same |
| `<?xml version="1.0"?><root><A>1</A></root>` | root{A='1'} | same |
| `<!DOCTYPE root><root><A>1</A></root>` | whole file rejected | root{A='1'} |
| `\xef\xbb\xbf<root><A>1</A></root>` | whole file rejected | root{A='1'} |
| `<root><Table><TableDescription source="x.xml"/></Table></root>` | root{Table{TableDescription source="x.xml"}} | root{Table{TableDescription}} |
| `<root><Table><A b="1">2</A></Table></root>` | root{Table{A='>2'}} | root{Table{A='2'}} |
| `<root><Table><A b='1'>2</A></Table></root>` | root{Table{A='>2'}} | root{Table{A='2'}} |
| `<root><Table><Xbox 360_Identifier>v</Xbox 360_Identifier></Table></root>` | whole file rejected | root{Table{Xbox 360_Identifier='v'}} |
| `<root><Table><A\tB>v</A\tB></Table></root>` | whole file rejected | root{Table{A	B='v'}} |
| `<root><Table><A >v</A ></Table></root>` | whole file rejected | root{Table{A='v'}} |
| `<root><Table><RampExposure>False</Exposure></Table></root>` | whole file rejected | root{Table{RampExposure='False'}} |
| `<root><Table><A>1</a></Table></root>` | root{Table{A='1'}} | same |
| `<root><Table><a>1</A></Table></root>` | root{Table{a='1'}} | same |
| `<root><Table><A>1</A></B></Table></root>` | whole file rejected | root{Table{A='1'}} |
| `<root><Table><A><B>1</A></Table></root>` | whole file rejected | root{Table{A{B='1'}}} |
| `<root><A>1</A>` | whole file rejected | root{A='1'} |
| `<root><A>1` | whole file rejected | root{A='1'} |
| `</x><root/>` | whole file rejected | root |
| `<root></root><root2/>` | root2 | root |
| `junk<root/>` | whole file rejected | same |
| `<root/>junk` | whole file rejected | root |
| `  \r\n<root/>\r\n` | root | same |
| `<root><Table><Name>09_PLayer\x1f_Neg</Name></Table></root>` | root{Table{Name='09_PLayer\x1f_Neg'}} | same |
| `<root><Table><A>\x01\x02</A></Table></root>` | root{Table{A}} | root{Table{A='\x01\x02'}} |
| `<root><Table><A>caf\xc3\xa9</A></Table></root>` | root{Table{A='caf\xe9'}} | root{Table{A='caf\xc3\xa9'}} |
| `<root><Table><A>\xff\xfe</A></Table></root>` | root{Table{A='\xff\xfe'}} | same |
| `<root><Table><A>a>b</A></Table></root>` | root{Table{A='a>b'}} | same |
| `<root><Table><A>a < b</A></Table></root>` | whole file rejected | root{Table{A='a < b'}} |
| `<root><Table><A>a<</A></Table></root>` | whole file rejected | root{Table{A='a<'}} |
| `<root><Table><A>1</A ></Table></root>` | root{Table='>'{A='1'}} | root{Table{A='1'}} |
| `<root><Table>< A>1</A></Table></root>` | root{Table{A='1'}} | root{Table='< A>1'} |
| `(empty)` | whole file rejected | same |
| ` ` | whole file rejected | same |
| `x` | whole file rejected | same |
| `<` | whole file rejected | same |
| `<root` | whole file rejected | same |
| `<root>` | whole file rejected | root |
| `<>` | whole file rejected | same |
| `<root><></root>` | whole file rejected | root='<>' |
| `<root>< /></root>` | root{} | root='< />' |
| `<root><Table><B> <C>2</C>y</B></Table></root>` | root{Table{B='y'{C='2'}}} | same |
| `<root><Table><B>x<!--c-->y</B></Table></root>` | root{Table{B='x'}} | root{Table{B='xy'}} |
| `<root><Table><B>x<?pi?>y</B></Table></root>` | root{Table{B='x'}} | root{Table{B='xy'}} |
| `<root><Table><A>\x1fv\x1f</A></Table></root>` | root{Table{A='v'}} | root{Table{A='\x1fv\x1f'}} |
| `<root><Table><A>\x7fv\x7f</A></Table></root>` | root{Table{A='\x7fv\x7f'}} | same |
| `<root><Table><A>\xa0v\xa0</A></Table></root>` | root{Table{A='\xa0v\xa0'}} | same |
| `<root><Table><A>a  b</A></Table></root>` | root{Table{A='a  b'}} | same |
| `<root><Table><A>a\r\nb</A></Table></root>` | root{Table{A='a\r\nb'}} | same |
| `<root><Table><A b>2</A></Table></root>` | root{Table{A='>2'}} | root{Table{A b='2'}} |
| `<root><Table><A b="1"/></Table></root>` | root{Table{A b="1"}} | root{Table{A}} |
| `<root><Table><A  >2</A></Table></root>` | whole file rejected | root{Table{A='2'}} |
| `<root><Table><A b="x>y">2</A></Table></root>` | root{Table{A='>y">2'}} | root{Table{A='2'}} |
| `<root><Table><A>&LT;</A></Table></root>` | whole file rejected | root{Table{A='&LT;'}} |
| `<root><Table><A>&amp;amp;</A></Table></root>` | root{Table{A='&amp;'}} | same |
| `<root><Table><A>\xc2\x80</A></Table></root>` | root{Table{A='\x80'}} | root{Table{A='\xc2\x80'}} |
| `<root><Table><A>\xc3\xbf</A></Table></root>` | root{Table{A='\xff'}} | root{Table{A='\xc3\xbf'}} |
| `<root><Table><A>\xc4\x80</A></Table></root>` | whole file rejected | root{Table{A='\xc4\x80'}} |
| `<root><Table><A>\xc5\x93</A></Table></root>` | whole file rejected | root{Table{A='\xc5\x93'}} |
| `<root><Table><A>\xc1\xbf</A></Table></root>` | root{Table{A='\x7f'}} | root{Table{A='\xc1\xbf'}} |
| `<root><Table><A>\xe2\x82\xac</A></Table></root>` | root{Table{A='\xe2\x82\xac'}} | same |
| `<root><Table><A>\xf0\x9f\x98\x80</A></Table></root>` | root{Table{A='\xf0\x9f\x98\x80'}} | same |
| `<root><Table><A>\xc3</A></Table></root>` | root{Table{A='\xc3'}} | same |
| `<root><Table><A>\xc3(</A></Table></root>` | root{Table{A='\xc3('}} | same |
| `<root><Table><A>\xc3\xc3\xa9</A></Table></root>` | root{Table{A='\xc3\xe9'}} | root{Table{A='\xc3\xc3\xa9'}} |
| `<a/><b/><c/>` | c | a |
| `<a><x/></a><b/>` | b | a{x} |
| `<root><Table><A/ ></Table></root>` | whole file rejected | root{Table{A/}} |
| `<root><Table><A / ></Table></root>` | whole file rejected | root{Table{A /}} |
| `<!--x--><root/>` | root | same |
| `<root/><!--x-->` | root | same |

## Census over the shipped tables

542 distinct extracted table files (TEAM A tools set). Files whose parse trees are identical: 95. Element-level differences (all files):

| kind | elements | files |
|---|---:|---:|
| whitespace-only text (engine: no text) | 1305 | 361 |
| other text | 126 | 126 |
| leading/trailing whitespace (engine trims) | 1268 | 87 |

Examples (file, element path, original text, rebuilt text):

| file | element | original | Team B |
|---|---|---|---|
| dlc1__car_4dr_compact01_yrn_veh.xtbl | /root/Table/Vehicle/Vehicle_Type/Automobile/AutomobileFlags | None | '\r\n\t\t\t\t\t' |
| dlc1__car_4dr_compact01_yrn_veh.xtbl | /root/TableTemplates | None | '\r\n\t' |
| dlc1__car_4dr_genki_veh.xtbl | /root/TableTemplates | None | '\r\n\t' |
| dlc1__dlc1_vehicle_cameras.xtbl | /root/TableTemplates | None | '\r\n\t' |
| dlc1__dlc1_vehicles.xtbl | /root/TableTemplates | None | '\r\n\t' |
| dlc1__sp_atv01_Genki_veh.xtbl | /root/TableTemplates | None | '\r\n\t' |
| dlc1__sp_genki_cannon_veh.xtbl | /root/Table/Vehicle/Vehicle_Type/Automobile/AutomobileFlags | None | '\r\n\t\t\t\t\t' |
| dlc1__sp_genki_cannon_veh.xtbl | /root/TableTemplates | None | '\r\n\t' |
| dlc1__car_4dr_compact01_yrn_veh.xtbl | /root/TableDescription | '>' | '\r\n\t' |
| dlc1__car_4dr_genki_veh.xtbl | /root/TableDescription | '>' | '\r\n\t' |
| dlc1__sp_atv01_Genki_veh.xtbl | /root/TableDescription | '>' | '\r\n\t' |
| dlc1__sp_genki_cannon_veh.xtbl | /root/TableDescription | '>' | '\r\n\t' |
| dlc1__sp_mdsphere_genki_veh.xtbl | /root/TableDescription | '>' | '\r\n\t' |
| dlc1__suv_4dr_07_yrn_veh.xtbl | /root/TableDescription | '>' | '\r\n\t' |
| dlc1__truck_4dr_pickup07_yrn_veh.xtbl | /root/TableDescription | '>' | '\r\n\t' |
| dlc2__bike_exotic01_genki_veh.xtbl | /root/TableDescription | '>' | '\r\n\t' |
| dlc2__dlc2_explosions.xtbl | /root/Table/Explosion/Sticky_Fire/Small_Sticky_Effects/Effect | '0' | '0\r\n\t\t\t\t\t' |
| dlc2__dlc2_explosions.xtbl | /root/Table/Explosion/Sticky_Fire/Small_Sticky_Effects/Effect | '0' | '0\r\n\t\t\t\t\t' |
| dlc2__dlc2_explosions.xtbl | /root/Table/Explosion/Sticky_Fire/Small_Sticky_Effects/Effect | '0' | '0\r\n\t\t\t\t\t' |
| dlc2__dlc2_explosions.xtbl | /root/Table/Explosion/Sticky_Fire/Small_Sticky_Effects/Effect | '0' | '0\r\n\t\t\t\t\t' |
| dlc2__dlc2_weapons.xtbl | /root/Table/Weapon/Vehicle_Weapon/Primary_Weapons/Weapon | '0' | '0\r\n\t\t\t\t\t' |
| dlc2__dlc2_weapons.xtbl | /root/TableTemplates/Weapon/Vehicle_Weapon/Primary_Weapons/Weapon | '0' | '0\r\n\t\t\t\t\t' |
| dlc2__heli_4dr_police_DLC.xtbl | /root/Table/Vehicle/Variants/Variant/Components/Component_Group/Component_Chances/Component_Chance/Components/Component_Element/Component | 'grime 75' | 'grime 75 ' |
| dlc2__heli_4dr_police_DLC.xtbl | /root/Table/Vehicle/Variants/Variant/Components/Component_Group/Component_Chances/Component_Chance/Components/Component_Element/Component | 'grime 75' | 'grime 75 ' |
