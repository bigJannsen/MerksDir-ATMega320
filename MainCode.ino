#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>
#include <avr/interrupt.h>
#include <stdint.h>

// ---------------- Pinbelegung ----------------
// LCD (4-Bit-Modus)
#define LCD_RS PB2
#define LCD_E  PB0
#define LCD_D4 PD4
#define LCD_D5 PD5
#define LCD_D6 PD6
#define LCD_D7 PD7

// LEDs
#define LED1 PD0
#define LED2 PD1
#define LED3 PD2
#define LED4 PD3

// Taster
#define T_RESET PC0  // Reset fürs Spiel
#define T1 PC1
#define T2 PC2
#define T3 PC3
#define T4 PC4

// Piezo
#define PIEZO PB3

// ---------------- Variablen ----------------
#define MAX_FOLGE 50
int merkfolge[MAX_FOLGE];   // merkt sich die Blinkreihenfolge
int laenge = 3;             // Startlänge der Folge
int spielstand = 0;         // Punktezahl

// ---------------- Timer / Zeit ----------------
// Zählt Millisekunden – braucht man für Zeitmessung & Timeout
volatile uint32_t ms_zaehler = 0;

// Interrupt, der jede Millisekunde aufgerufen wird
ISR(TIMER1_COMPA_vect) {
    ms_zaehler++;
}

// Timer1 aktivieren – läuft mit 1ms Takt
// CTC-Modus = Clear Timer on Compare (praktisch für millis)?
void uhr_anwerfen(void) {
    TCCR1B |= (1 << WGM12);              // CTC Mode
    TCCR1B |= (1 << CS11) | (1 << CS10); // Prescaler 64
    OCR1A = 250;                         // 1ms bei 16 MHz
    TIMSK1 |= (1 << OCIE1A);
    sei();                               // Interrupts an
}

// Gibt aktuelle Zeit in ms zurück
uint32_t zeit_ms(void) {
    uint32_t ms;
    cli();
    ms = ms_zaehler;
    sei();
    return ms;
}

// ---------------- LCD ----------------
// Kurzer Puls auf Enable-Pin
void lcd_anfangspuls(void) {
    PORTB |= (1 << LCD_E);
    _delay_us(1);
    PORTB &= ~(1 << LCD_E);
    _delay_us(100);
}

// 4Bit an LCD
void lcd_communicator(uint8_t teil) {
    (teil & 0x01) ? (PORTD |= (1 << LCD_D4)) : (PORTD &= ~(1 << LCD_D4));
    (teil & 0x02) ? (PORTD |= (1 << LCD_D5)) : (PORTD &= ~(1 << LCD_D5));
    (teil & 0x04) ? (PORTD |= (1 << LCD_D6)) : (PORTD &= ~(1 << LCD_D6));
    (teil & 0x08) ? (PORTD |= (1 << LCD_D7)) : (PORTD &= ~(1 << LCD_D7));
    lcd_anfangspuls();
}

// Schickt Befehle (z. B. Cursor, Clear usw.)
void lcd_befehlsgeber(uint8_t befehl) {
    PORTB &= ~(1 << LCD_RS);
    lcd_communicator(befehl >> 4);
    lcd_communicator(befehl & 0x0F);
    _delay_ms(2);
}

// Schreibt Zeichen
void lcd_zeichen(uint8_t zeichen) {
    PORTB |= (1 << LCD_RS);
    lcd_communicator(zeichen >> 4);
    lcd_communicator(zeichen & 0x0F);
    _delay_ms(2);
}

// Display löschen
void lcd_clear(void) {
    lcd_befehlsgeber(0x01);
    _delay_ms(2);
}

// Cursorposition setzen
void lcd_cursorsetzer(uint8_t x, uint8_t y) {
    uint8_t addr = (y == 0 ? 0x00 : 0x40) + x; // wichtig: Zeilenversatz
    lcd_befehlsgeber(0x80 | addr);
}

// Text schreiben
void lcd_schreiben(const char *text) {
    while (*text) lcd_zeichen(*text++);
}

// LCD initialisieren (4-Bit-Modus aktivieren)
// Dauert am Anfang ein paar ms, Display will "aufwachen"
void lcd_hochfahren(void) {
    // |= Kurzschreibweise für xxx = xxx | bedingung
    DDRB |= (1 << LCD_RS) | (1 << LCD_E) | (1 << PIEZO);
    DDRD |= (1 << LCD_D4) | (1 << LCD_D5) | (1 << LCD_D6) | (1 << LCD_D7)
          | (1 << LED1) | (1 << LED2) | (1 << LED3) | (1 << LED4);

    _delay_ms(40);
    lcd_communicator(0x03);
    _delay_ms(5);
    lcd_communicator(0x03);
    _delay_us(150);
    lcd_communicator(0x03);
    _delay_us(150);
    lcd_communicator(0x02);
    _delay_us(150);

    lcd_befehlsgeber(0x28); // 2 Zeilen, 5x8 Font
    lcd_befehlsgeber(0x08); // Display aus
    lcd_befehlsgeber(0x01); // löschen
    _delay_ms(2);
    lcd_befehlsgeber(0x06); // Cursor nach rechts
    lcd_befehlsgeber(0x0C); // Display an, Cursor aus
}

// ---------------- Piezo ----------------
// Macht ein "Biep" mit Frequenz Dauer und Lautstärke
void bieper(uint16_t freq, uint16_t dauer, uint8_t laut) {
    if (freq == 0) {
        _delay_ms(dauer);
        return;
    }

    uint32_t periode = 1000000UL / freq;
    uint32_t high = (periode * laut) / 30; // -> Zahl höher = Lautstärke generell leiser
    uint32_t low = periode - high;
    uint32_t zyklen = ((uint32_t)dauer * 1000UL) / periode;

    for (uint32_t i = 0; i < zyklen; i++) {
        PORTB |= (1 << PIEZO);
        for (uint32_t j = 0; j < high; j++) _delay_us(1);
        PORTB &= ~(1 << PIEZO);
        for (uint32_t j = 0; j < low; j++) _delay_us(1);
    }
}

// ---------------- Spielkramzeugs ----------------
// Alle LEDs aus
void alle_leds_aus(void) {
    PORTD &= ~((1 << LED1)|(1 << LED2)|(1 << LED3)|(1 << LED4));
}

// Einzelne LED an
void led_an(uint8_t welche) {
    PORTD |= (1 << welche);
}

// Prüft ob Reset gedrückt wurd
uint8_t reset_gedrueckt(void) {
    if (!(PINC & (1 << T_RESET))) {
        _delay_ms(20);
        if (!(PINC & (1 << T_RESET))) return 1;
    }
    return 0;
}

// Wartet auf Tastendruck oder Timeout / Reset
int warte_auf_spieler(uint32_t timeout) {
    uint32_t start = zeit_ms();
    while (1) {
        if (reset_gedrueckt()) return -2;
        if ((zeit_ms() - start) > timeout) return -1; // Timeout

        if ((PINC & (1 << T1)) == 0) {
            _delay_ms(20);
            while ((PINC & (1 << T1)) == 0) {} return 0; }
        if ((PINC & (1 << T2)) == 0) {
            _delay_ms(20);
            while ((PINC & (1 << T2)) == 0) {} return 1; }
        if ((PINC & (1 << T3)) == 0) {
            _delay_ms(20);
            while ((PINC & (1 << T3)) == 0) {} return 2; }
        if ((PINC & (1 << T4)) == 0) {
            _delay_ms(20);
            while ((PINC & (1 << T4)) == 0) {} return 3; }
        }       // Für Einzeiler entschieden, weil Langform vieeeel zu lang
}

// Neue zufällige Blinkfolge basteln
void zufallsfolge(int laenge) {
    for (int i = 0; i < laenge; i++)
        merkfolge[i] = rand() % 4;
}

// Zeigt aktuellen Punktestand
void lcd_aktuellerscore(void) {
    char buf[6];
    lcd_cursorsetzer(0, 1);
    lcd_schreiben("Punkte: ");
    itoa(spielstand, buf, 10);
    lcd_schreiben(buf);
    lcd_schreiben("   ");
}

// Spielt die Blink- und Tonfolge ab
void led_blinkreihenfolge(int laenge) {
    lcd_clear();
    lcd_schreiben("Merken...");
    lcd_aktuellerscore();
    _delay_ms(500);

    for (int i = 0; i < laenge; i++) {
        led_an(merkfolge[i]);
        switch (merkfolge[i]) {
            case 0: bieper(329, 300, 5); break;
            case 1: bieper(261, 300, 5); break;
            case 2: bieper(392, 300, 5); break;
            case 3: bieper(523, 300, 5); break;
        }
        alle_leds_aus();
        _delay_ms(200);
    }
}

// Prüftob Spieler die richtige Reihenfolge drückt
uint8_t spieler_eingabe(int laenge) {
    lcd_clear();
    lcd_schreiben("Dein Zug...");
    lcd_aktuellerscore();

    for (int i = 0; i < laenge; i++) {
        int gedrueckt = warte_auf_spieler(20000);
        if (gedrueckt == -2) return 2;  // Reset
        if (gedrueckt == -1) return 3;  // Timeout

        led_an(gedrueckt);
        switch (gedrueckt) {
            case 0: bieper(329, 300, 5); break;
            case 1: bieper(261, 300, 5); break;
            case 2: bieper(392, 300, 5); break;
            case 3: bieper(523, 300, 5); break;
        }
        alle_leds_aus();

        if (gedrueckt != merkfolge[i]) return 0; // Falsch
    }
    return 1;
}

// ---------------- Hauptlogik vom Spiel ----------------
void spiel_geht_los(void) {
    lcd_clear();
    lcd_schreiben("Let's go!");
    _delay_ms(1000);

    spielstand = 0;
    laenge = 3;
    srand(zeit_ms()); // Zufall auch nach Neustart???

    while (1) {
        zufallsfolge(laenge);
        led_blinkreihenfolge(laenge);
        uint8_t ergebnis = spieler_eingabe(laenge);

        if (ergebnis == 1) { // Richtig
            spielstand++;
            lcd_clear();
            lcd_schreiben("Richtig!");
            lcd_aktuellerscore();
            _delay_ms(700);
            laenge++;
        }
        else if (ergebnis == 0) { // Falsch
            lcd_clear();
            lcd_schreiben("Uuuh falsch!");
            bieper(500, 300, 5);
            bieper(300, 500, 5);
            _delay_ms(1000);
            lcd_clear();
            lcd_schreiben("Game Over");
            lcd_cursorsetzer(0,1);
            lcd_schreiben("Punkte: ");
            char buf[6];
            itoa(spielstand, buf, 10);
            lcd_schreiben(buf);
            _delay_ms(2500);
            break;
        }
        else if (ergebnis == 2) { // Reset gedrückt
            lcd_clear();
            lcd_schreiben("Abgebrochen!");
            bieper(600, 300, 5);
            _delay_ms(1500);
            break;
        }
        else if (ergebnis == 3) { // Timeout
            lcd_clear();
            lcd_schreiben("Zu spaet!");
            bieper(300, 500, 5);
            _delay_ms(1500);
            break;
        }
    }
}

// ---------------- Hauptprogramm -----------
int main(void) {
    // Taster als Eingang, Pull-Ups aktiv
    DDRC &= ~((1<<T1)|(1<<T2)|(1<<T3)|(1<<T4)|(1<<T_RESET));
    PORTC |= (1<<T1)|(1<<T2)|(1<<T3)|(1<<T4)|(1<<T_RESET);

    lcd_hochfahren();
    uhr_anwerfen();

    lcd_schreiben("== MerksDIR ==");
    _delay_ms(1500);
    lcd_clear();
    lcd_schreiben("Drueck Reset...");

    while (1) {
        if (!(PINC & (1<<T_RESET))) {
            _delay_ms(20);
            while (!(PINC & (1<<T_RESET)));
            spiel_geht_los();
            lcd_clear();
            lcd_schreiben("Drueck Reset...");
        }
    }
}
