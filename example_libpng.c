// Per compilare aggiungere "-lpng" su linux
// Su MacOS bisogna dire dove si trovano gli header e le librerie, con
// l'installazione delle librerie tramite homebrew quindi il comando diventa
// così "clang example_libpng.c -o example -I/opt/homebrew/include
// -L/opt/homebrew/lib -lpng -lz"

#include <png.h> // Include per usare le funzioni della libreria libpng
#include <stdio.h> // Include per funzioni di input/output come fopen, fclose, etc.
#include <stdlib.h> // Include per funzioni di allocazione dinamica e gestione di memoria

#define ERROR_PNG_STRUCT_READ_CREATION 2
#define ERROR_PNG_STRUCT_WRITE_CREATION 3
#define ERROR_PNG_INFO_STRUCT_CREATION 4
#define ERROR_PNG_READ_ELABORATION 5
#define ERROR_PNG_WRITE_ELABORATION 6

// Variabili globali per larghezza, altezza e proprietà dell'immagine PNG
int width, height;
png_byte color_type;            // Tipo di colore (es. RGB, RGBA)
png_byte bit_depth;             // Profondità del colore (es. 8 bit, 16 bit)
png_bytep *row_pointers = NULL; // Puntatore per le righe dell'immagine

// Funzione per leggere un file PNG senza canale alpha
void read_png_file(char *filename) {
  FILE *fp = fopen(filename, "rb"); // Apre il file PNG in modalità binaria

  if (!fp)
    exit(EXIT_FAILURE);

  // Crea una struttura per leggere il PNG
  png_structp png =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png)
    exit(ERROR_PNG_STRUCT_READ_CREATION); // Termina se la struttura non viene
                                          // creata

  // Crea una struttura per le informazioni del PNG
  png_infop info = png_create_info_struct(png);
  if (!info)
    exit(ERROR_PNG_INFO_STRUCT_CREATION); // Termina se non si riesce a creare
                                          // la struttura delle info

  // Imposta il salto in caso di errore
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    exit(ERROR_PNG_READ_ELABORATION);
  }

  // Inizializza la lettura del file PNG
  png_init_io(png, fp);

  // Legge le informazioni principali sull'immagine (dimensioni, tipo di colore,
  // ecc.)
  png_read_info(png, info);

  // Estrae le proprietà dell'immagine
  width = png_get_image_width(png, info);     // Ottiene la larghezza
  height = png_get_image_height(png, info);   // Ottiene l'altezza
  color_type = png_get_color_type(png, info); // Ottiene il tipo di colore
  bit_depth = png_get_bit_depth(png, info); // Ottiene la profondità del colore

  // Converte il formato colore in un formato standard a 8 bit, RGB
  if (bit_depth == 16)
    png_set_strip_16(png); // Riduce la profondità a 8 bit se era a 16 bit

  if (color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png); // Converte la palette di colori in RGB

  // Espande i valori di profondità bassa (1, 2, 4 bit) in 8 bit per immagini in
  // scala di grigi
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    png_set_expand_gray_1_2_4_to_8(png);

  // Converte i colori in scala di grigi in RGB
  if (color_type == PNG_COLOR_TYPE_GRAY ||
      color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png);

  // Rimuove qualsiasi canale alpha e legge solo i valori RGB
  png_set_strip_alpha(png); // Elimina il canale alpha se presente

  // Aggiorna le informazioni del PNG dopo le conversioni
  png_read_update_info(png, info);

  // Alloca la memoria per contenere le righe dell'immagine
  row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
  for (int y = 0; y < height; y++) {
    row_pointers[y] =
        (png_byte *)malloc(png_get_rowbytes(png, info)); // Alloca una riga
  }

  // Legge i pixel dell'immagine nel buffer 'row_pointers'
  png_read_image(png, row_pointers);

  // Chiude il file
  fclose(fp);

  // Libera le strutture allocate per leggere l'immagine
  png_destroy_read_struct(&png, &info, NULL);
}

// Funzione per scrivere un file PNG
void write_png_file(char *filename) {
  FILE *fp = fopen(filename,
                   "wb"); // Apre il file per la scrittura in modalità binaria
  if (!fp)
    exit(EXIT_FAILURE);

  // Crea una struttura per scrivere il PNG
  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png)
    exit(ERROR_PNG_STRUCT_WRITE_CREATION);

  // Crea una struttura per le informazioni del PNG
  png_infop info = png_create_info_struct(png);
  if (!info)
    exit(ERROR_PNG_INFO_STRUCT_CREATION);

  // Imposta il salto in caso di errore
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    exit(ERROR_PNG_WRITE_ELABORATION);
  }

  // Inizializza l'output per scrivere nel file
  png_init_io(png, fp);

  // Imposta le informazioni dell'immagine di output (larghezza, altezza,
  // formato RGBA)
  png_set_IHDR(png, info, width, height,
               8,                            // 8 bit di profondità
               PNG_COLOR_TYPE_RGB,           // Formato colore RGBA
               PNG_INTERLACE_NONE,           // Senza interlacciamento
               PNG_COMPRESSION_TYPE_DEFAULT, // Compressione di default
               PNG_FILTER_TYPE_DEFAULT       // Filtro di default
  );
  png_write_info(png, info); // Scrive le informazioni dell'immagine nel file

  // Controlla se le righe sono state allocate
  if (!row_pointers)
    abort();

  // Scrive i dati dell'immagine
  png_write_image(png, row_pointers);
  png_write_end(png, NULL); // Termina la scrittura

  // Libera la memoria delle righe allocate
  for (int y = 0; y < height; y++) {
    free(row_pointers[y]);
  }
  free(row_pointers);

  // Chiude il file
  fclose(fp);

  // Libera le strutture allocate per la scrittura dell'immagine
  png_destroy_write_struct(&png, &info);
}

// Funzione per processare l'immagine (modifica pixel se necessario)
void process_png_file() {
  printf("Column, Row, RGB\n");
  for (int y = 0; y < height; y++) {
    png_bytep row = row_pointers[y]; // Ottiene una riga
    for (int x = 0; x < width; x++) {
      png_bytep px = &(row[x * 3]); // Ottiene il pixel (formato RGBA)
      // Qui è possibile eseguire operazioni sui pixel
      printf("%4d, %4d = RGB(%3d, %3d, %3d)\n", x, y, px[0], px[1], px[2]);
    }
  }
}

// Funzione principale
int main(int argc, char *argv[]) {
  if (argc != 2)
    exit(EXIT_FAILURE); // Verifica che ci siano 2 argomenti (programma, file
                        // input)

  read_png_file(argv[1]); // Legge il file PNG di input
  process_png_file();     // Processa l'immagine
  if (argc == 3) {
    write_png_file(argv[2]); // Scrive l'immagine su file di output
  }

  return 0;
}
