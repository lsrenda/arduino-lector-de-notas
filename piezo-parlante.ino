// ---------- Pins ----------
const int PIN_PIEZO = 8;
const int PIN_MIC   = A0;          // KY-037 AO

// ---------- Melody: Ode to Joy ----------
const int C4 = 262, D4 = 294, E4 = 330, F4 = 349, G4 = 392;

const int melodia[] = {
  E4, E4, F4, G4,  G4, F4, E4, D4,
  C4, C4, D4, E4,  E4, D4, D4,
  E4, E4, F4, G4,  G4, F4, E4, D4,
  C4, C4, D4, E4,  D4, C4, C4
};
const float duracion[] = {
  1, 1, 1, 1,  1, 1, 1, 1,
  1, 1, 1, 1,  1.5, 0.5, 2,
  1, 1, 1, 1,  1, 1, 1, 1,
  1, 1, 1, 1,  1.5, 0.5, 2
};
const int NOTAS = sizeof(melodia) / sizeof(melodia[0]);

const int TEMPO_MS = 600;          // one quarter note (slower than before so notes can be told apart)
const int GAP_MS   = 120;          // silence at the end of each note
const unsigned long PAUSA_FINAL = 2000;

unsigned long proximaNota = 0;
int idxNota = 0;

// ---------- Listener ----------
const int N = 320;                 // samples per window
int muestras[N];

const int UMBRAL_PP = 20;          // minimum peak-to-peak (ADC counts) to count as sound
const float F_MIN = 150, F_MAX = 1000;
const bool DEBUG_PP = false;       // true = print the signal amplitude to calibrate

const char* NOMBRES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

int notaConfirmada = -1;           // MIDI note currently reported (-1 = none)
int candidata = -1;
int aciertos = 0;

// ---------- Melody (non-blocking) ----------
void tocarMelodia() {
  if (millis() < proximaNota) return;

  int dur = TEMPO_MS * duracion[idxNota];
  if (melodia[idxNota] > 0) tone(PIN_PIEZO, melodia[idxNota], dur - GAP_MS);

  proximaNota = millis() + dur;
  idxNota++;
  if (idxNota >= NOTAS) {
    idxNota = 0;
    proximaNota += PAUSA_FINAL;
  }
}

// ---------- Frequency detection ----------
// Returns: -1 = silence, 0 = sound without a clear pitch, >0 = frequency in Hz
float detectarFrecuencia() {
  unsigned long t0 = micros();
  for (int i = 0; i < N; i++) muestras[i] = analogRead(PIN_MIC);
  float usPorMuestra = (micros() - t0) / (float)N;

  long suma = 0;
  int mn = 1023, mx = 0;
  for (int i = 0; i < N; i++) {
    suma += muestras[i];
    if (muestras[i] < mn) mn = muestras[i];
    if (muestras[i] > mx) mx = muestras[i];
  }
  int pp = mx - mn;
  if (DEBUG_PP) { Serial.print("pp="); Serial.println(pp); }
  if (pp < UMBRAL_PP) return -1;

  int media = suma / N;
  int hist = pp / 4;                       // hysteresis against noise
  int umbralAlto = media + hist;
  int umbralBajo = media - hist;

  bool alto = muestras[0] > umbralAlto;
  int cruces = 0;
  float primero = 0, ultimo = 0;

  for (int i = 1; i < N; i++) {
    if (!alto && muestras[i] > umbralAlto) {
      alto = true;
      // linear interpolation for the exact crossing position
      float pos = (i - 1) + (float)(umbralAlto - muestras[i - 1]) / (muestras[i] - muestras[i - 1]);
      if (cruces == 0) primero = pos;
      ultimo = pos;
      cruces++;
    } else if (alto && muestras[i] < umbralBajo) {
      alto = false;
    }
  }

  if (cruces < 4) return 0;
  float periodo = (ultimo - primero) / (cruces - 1);   // in samples
  return 1e6 / (periodo * usPorMuestra);
}

int frecuenciaAMidi(float f) {
  return (int)round(69.0 + 12.0 * log(f / 440.0) / 0.693147);
}

void imprimirNota(int midi, float f) {
  int idx = ((midi % 12) + 12) % 12;
  int octava = midi / 12 - 1;
  float ideal = 440.0 * pow(2.0, (midi - 69) / 12.0);
  int cents = (int)round(1200.0 * log(f / ideal) / 0.693147);

  Serial.print("Heard: ");
  Serial.print(NOMBRES[idx]);
  Serial.print(octava);
  Serial.print("  (");
  Serial.print((int)round(f));
  Serial.print(" Hz, ");
  if (cents >= 0) Serial.print('+');
  Serial.print(cents);
  Serial.println(" cents)");
}

void setup() {
  pinMode(PIN_PIEZO, OUTPUT);
  Serial.begin(9600);
  Serial.println("Listening...");
}

void loop() {
  tocarMelodia();

  float f = detectarFrecuencia();

  if (f < 0) {                               // silence: ready to report the same note again
    notaConfirmada = -1;
    candidata = -1;
    aciertos = 0;
    return;
  }
  if (f < F_MIN || f > F_MAX) {              // noise without a usable pitch
    candidata = -1;
    aciertos = 0;
    return;
  }

  int midi = frecuenciaAMidi(f);
  if (midi == candidata) aciertos++;
  else { candidata = midi; aciertos = 1; }

  if (aciertos >= 2 && midi != notaConfirmada) {
    notaConfirmada = midi;
    imprimirNota(midi, f);
  }
}
