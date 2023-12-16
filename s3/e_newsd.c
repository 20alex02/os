/* TBD. */


/* Popis formátu
 *
 * DNS používá pro své zprávy binární formát. Jelikož se jedná
 * o relativně komplexní protokol, tento formát obsahuje mnoho
 * hodnot s různými významy. Mnoho z nich pouze nastavíme na
 * výchozí hodnoty nebo úplně přeskočíme.
 *
 * Tento formát se skládá z jednobajtových, dvojbajtových
 * a čtyřbajtových hodnot. Všechny dvojbajtové hodnoty a čtyřbajtové
 * hodnoty jsou ve zprávách uloženy ve formátu, že nejvýznamnější
 * bajt je první (jako je pro síťový přenos obvyklé).
 *
 * Každá zpráva DNS je stejného formátu, který se neliší dle toho,
 * ze které strany pochází (na rozdíl například od HTTP, kde se
 * rozlišují požadavek a odpověď).
 *
 * Zpráva DNS sestává z několika částí:
 *
 * ┌──────────┬────────┬──────────┬──────────┬────────────┐
 * │ hlavička │ dotazy │ odpovědi │ autorita │ doplňující │
 * └──────────┴────────┴──────────┴──────────┴────────────┘
 *
 * Pro tento úkol jsou relevantní pouze «první tři». Hlavička je
 * v každé zprávě pouze jedna, nicméně dotazů i odpovědí může být
 * více. Pro tento úkol se však omezíme pouze na jeden dotaz
 * a zpracování jen první odpovědi. Ostatní části budeme ignorovat.
 *
 * Formát hlavičky
 *
 * První sekcí zprávy je hlavička, která obsahuje šest hodnost po
 * dvou bajtech (jak řečeno výše, významnější bajt je vždy první):
 *
 *  1. ‹ID› – identifikátor pro přiřazení odpovědi k dotazu;
 *  2. ‹FLAGS› – bitová mapa s několika příznaky;
 *  3. ‹QDCOUNT› – počet dotazů (pro nás bude roven 1);
 *  4. ‹ANCOUNT› – počet odpovědí;
 *  5. ‹NSCOUNT› – počet odpovědí odkazující na autoritativní jmenné
 *     servery;
 *  6. ‹ARCOUNT› – počet doplňujících záznamů.
 *
 * Hodnoty 5 a 6 budeme ignorovat, neboť nás záznamy těchto typů
 * nezajímají.
 *
 * Zde je bližší popis dvojbajtové bitmapy ‹FLAGS› a co které
 * příznaky znamenají. Vrchní čísla korespondují s pořadím bitu
 * a nulou je označen ten nejméně významný.
 *
 *       15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 *      ┌──┬──┴──┴──┴──┬──┬──┬──┬──┬──┴──┴──┬──┴──┴──┴──┐
 *      │QR│  OPCODE   │AA│TC│RD│RA│   Z    │   RCODE   │
 *      └──┴───────────┴──┴──┴──┴──┴────────┴───────────┘
 *
 *  • ‹QR› – bit značící, zda se jedná o dotaz, či odpověď (‹0/1›);
 *  • ‹OPCODE› – ‹0› značí, že se jedná o standardní dotaz, jinou
 *    hodnotu zde nečekáme;
 *  • ‹AA› – bit značící, zda se jedná o autoritativní odpověď;
 *  • ‹TC› – bit značící, zda došlo k ořezání zprávy;
 *  • ‹RD› – bit značící, zda pokládáme rekurzivní dotaz;
 *  • ‹RA› – v odpovědi značí, zda je rekurzivní dotaz dostupný;
 *  • ‹Z› – nepoužito, nastavíme na ‹0›;
 *  • ‹RCODE› – v odpovědi indikuje, zda byl dotaz úspěšný.
 *
 * Příznaky ‹AA›, ‹TC›, ‹RD› a ‹RA› jsou aktivní pro bit roven ‹1›.
 *
 * Pro ‹RCODE› se setkáte s hodnotami:
 *
 *  • ‹0› – úspěch;
 *  • ‹1› – chybný formát dotazu;
 *  • ‹2› – problém se jmenným serverem;
 *  • ‹3› – doména neexistuje;
 *  • ‹4› – nepodporováno, server neumožňuje takový dotaz;
 *  • ‹5› – odmítnuto, server podle svých nastavených zásad odmítá
 *    poskytnout odpověď.
 *  • ‹6› až ‹15› jsou buď rezervované do budoucna nebo pro tento
 *    úkol nerelevantní.
 *
 * Jelikož nechceme provádět rekurzivní dotaz manuálně, nastavíme
 * v dotazu příznak ‹RD› na ‹1›, ať to za nás provede dotazovaný
 * server. Ostatní příznaky vynulujeme.
 *
 * Formát dotazu
 *
 * Po hlavičce zpráva obsahuje dotazy, které jsou následujícího
 * formátu. Počet těchto dotazů je indikován výše uvedeným
 * ‹QDCOUNT›. V tomto úkolu pošleme vždy pouze jeden dotaz.
 *
 *        N₁ bajtů       2 B     2 B
 *   ┌────────────────┬───────┬────────┐
 *   │ doménové jméno │ QTYPE │ QCLASS │
 *   └────────────────┴───────┴────────┘
 *
 * Jelikož se jedná o binární formát, doménové jméno není uvedeno
 * v textové formě, jako je to obvyklé např. v prohlížečích, nýbrž
 * je zakódováno speciálním způsobem.
 *
 * Doménové části oddělené tečkami (označované anglicky jako
 * „labels“) jsou zapsány tak, že tečky jsou vynechány a každé části
 * místo toho předchází bajt označující její délku. Zároveň za
 * poslední částí se nachází nulový bajt pro označení konce. Samotné
 * znaky nesoucí název části zůstávají kódované v ASCII.
 *
 * Například doménové jméno „muni.cz“ by do dotazu bylo vloženo jako
 * posloupnost bajtů:
 *
 *     { 0x04, 'm', 'u', 'n', 'i', 0x02, 'c', 'z', 0x00 }
 *
 * Po takto zakódovaném doménovém jménu následují dvě dvoubajtové
 * číselné hodnoty: ‹QTYPE› a ‹QCLASS›
 *
 * První určuje, na jaký typ informace se dotazujeme. Jelikož chceme
 * zjistit adresu IP ve verzi 6, je pro nás relevantní hodnota ‹28›,
 * která odpovídá typu označovaném jako „AAAA“, tedy adrese IPv6¹.
 *
 * Hodnota ‹QCLASS› určuje, z jaké třídy chceme znát informaci. Tou
 * je pro nás Internet (označení „IN“), které odpovídá hodnota ‹1›.
 *
 * Formát odpovědi
 *
 * Po dotazech se nachází ve zprávě nula nebo více odpovědí, které
 * mají složitější formát:
 *
 *       N₂ bajtů      2 B     2 B    4 B     2 B     RDLENGTH bajtů
 *  ┌────────────────┬──────┬───────┬─────┬──────────┬──────────────┐
 *  │ doménové jméno │ TYPE │ CLASS │ TTL │ RDLENGTH │     RDATA    │
 *  └────────────────┴──────┴───────┴─────┴──────────┴──────────────┘
 *
 * První pole obsahuje doménové jméno, ke kterému se váže zbytek
 * odpovědi. Jelikož tuto informaci nechceme ukládat do výsledku,
 * můžeme ji v klidu přeskočit. Zde však nastává rozdíl oproti
 * formátu doménového jména uvedeného výše. Kromě toho, že zde mohou
 * být části domény („labels“) uvedené délkovým bajtem, je navíc
 * definován způsob, jak doménová jména „komprimovat“.
 *
 * To pouze znamená, že konec doménového jména bude buď nulový bajt
 * jako ve formátu výše, nebo speciální dvojice bajtů, z nichž
 * u prvního budou «dva» nejvýznamnější bity rovny ‹1› a druhý může
 * být libovolný.
 *
 * Následují dvě hodnoty ‹TYPE› a ‹CLASS›, které jsou podobného
 * významu jako ‹QTYPE› a ‹QCLASS› výše. Budeme u nich očekávat
 * stejné hodnoty jako byly přiřazeny v dotazu. Tedy ‹TYPE› by měl
 * být roven ‹1› a ‹CLASS› by měl mít hodnotu ‹28›.
 *
 * Čtyřbajtová hodnota ‹TTL› značí v sekundách, na jak dlouho můžeme
 * obdrženou odpověď považovat za platnou.
 *
 * Nakonec následují samotná data uvedená dvěma bajty popisující
 * jejich velikost. To jsou pole ‹RDLENGTH› a ‹RDATA›. Pro adresu
 * IPv6 (tzn. ‹TYPE=28› a ‹CLASS=1›) očekáváme velikost 16 bajtů.
 *
 * Doporučení na konec
 *
 *  • Funkčnost svého řešení si můžete ověřit u některého veřejného
 *    poskytovatele;
 *  • v případě, že pošlete chybný dotaz, je možné, že někteří
 *    poskytovatelé paket budou ignorovat a neodešlou ani odpověď
 *    s indikací chyby;
 *  • je rovněž možné, že při vysokém množství požadavků dostanete
 *    „timeout“ a daný poskytovatel Vám přestane odpovídat;
 *  • pro snazší ladění doporučujeme implementovat si vlastní
 *    pomocné procedury na výpis obsahu hlaviček a dalších částí
 *    zpráv;
 *  • užitečná může být knihovní funkce ‹inet_ntop›, která převede
 *    internetovou adresu z binární formy na řetězec pro výpis.
 *
 *  ¹ Kdyby nás zajímala adresa IPv4, hodnota by byla ‹1› a typ by
 *    byl označovaný jako „A“. */
