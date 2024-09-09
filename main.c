// Per compilare aggiungere "-lpng" su linux
// Su MacOS bisogna dire dove si trovano gli header e le librerie, con
// l'installazione delle librerie tramite homebrew quindi il comando diventa
// così "clang example_libpng.c -o example -I/opt/homebrew/include
// -L/opt/homebrew/lib -lpng -lz"

#include <png.h> // Include per usare le funzioni della libreria libpng
#include <stdio.h> // Include per funzioni di input/output come fopen, fclose, etc.
#include <stdlib.h> // Include per funzioni di allocazione dinamica e gestione di memoria

#define ERROR_PNG_STRUCT_WRITE_CREATION 2
#define ERROR_PNG_INFO_STRUCT_CREATION 3
#define ERROR_PNG_WRITE_ELABORATION 4
#define ERROR_ROWS_NOT_ALLOCATED 5

// Risoluzione di default = 4K (Ultra HD) -> 33 177 600 bytes, 265 420 800 bits
#define WIDTH_DEFAULT 3840
#define HEIGHT_DEFAULT 2160
#define BYTES_PER_PIXEL 4

// Variabili globali
int width = WIDTH_DEFAULT;
int height = HEIGHT_DEFAULT;
png_bytep *row_pointers = NULL; // Puntatore per le righe dell'immagine

void convert_file() {
  // Pensavo di utilizzare i primi 4 byte di un immagine per definire in maniera
  // precisa quando un file termina e quindi dopo iniziano i pixel di
  // riempimento.
  // Utilizzo 12 bits per la colonna e 12 bits per la riga -> 24
  // bits => primi 3 byte della foto.
  // Del byte sucessivo poi utilizzavo 2 bits
  // per indicare in quale byte di un pixel termina il file
  // primo bit      =   R
  // secondo bit    =   G
  // terzo bit      =   B
  // quarto bit     =   A
  // Memorizzo l'estensione del file che salvo, utilizzo i restanti 6 bit che
  // avanzano per salvare la lunghezza in decimale dell'estensione del file
  // questo comporta che si può avere massimo 64 caratteri per l'estensione del file
  // che ritengo abbondanti

  // Alloca la memoria per contenere le righe dell'immagine
  row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
  for (int y = 0; y < height; y++) {
    row_pointers[y] = (png_byte *)malloc(
        BYTES_PER_PIXEL * width); // Alloca una riga, 4 byte per pixel (RGBA)
  }
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
               PNG_COLOR_TYPE_RGBA,          // Formato colore RGBA
               PNG_INTERLACE_NONE,           // Senza interlacciamento
               PNG_COMPRESSION_TYPE_DEFAULT, // Compressione di default
               PNG_FILTER_TYPE_DEFAULT       // Filtro di default
  );
  png_write_info(png, info); // Scrive le informazioni dell'immagine nel file

  // Controlla se le righe sono state allocate
  if (!row_pointers)
    exit(ERROR_ROWS_NOT_ALLOCATED);

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

int main(int argc, char *argv[]) {
  FILE *fp =
      fopen(argv[1],
            "rb"); // Apre il file per la scrittura in modalità lettura binaria
  if (!fp) {
    printf("File not found\n");
    exit(EXIT_FAILURE);
  }

  return 0;
}
