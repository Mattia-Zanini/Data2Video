#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int width = 250;               // Larghezza dell'immagine
int height = 250;               // Altezza dell'immagine
png_bytep *row_pointers = NULL; // Puntatore per le righe dell'immagine

// Funzione per scrivere un file PNG
void write_png_file(char *filename) {
  FILE *fp = fopen(filename, "wb");
  if (!fp)
    exit(EXIT_FAILURE);

  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png)
    exit(EXIT_FAILURE);

  png_infop info = png_create_info_struct(png);
  if (!info)
    exit(EXIT_FAILURE);

  if (setjmp(png_jmpbuf(png)))
    abort();

  png_init_io(png, fp);

  // Imposta le informazioni dell'immagine (larghezza, altezza, formato RGBA)
  png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  png_write_info(png, info);

  // Scrive i dati dell'immagine
  png_write_image(png, row_pointers);
  png_write_end(png, NULL);

  // Libera la memoria delle righe allocate
  for (int y = 0; y < height; y++) {
    free(row_pointers[y]);
  }
  free(row_pointers);

  fclose(fp);
  png_destroy_write_struct(&png, &info);
}

// Funzione per creare l'immagine con pixel randomici
void create_random_image() {
  row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
  for (int y = 0; y < height; y++) {
    row_pointers[y] = (png_byte *)malloc(width * 4); // 4 byte per pixel (RGBA)
    png_bytep row = row_pointers[y];
    for (int x = 0; x < width; x++) {
      png_bytep px = &(row[x * 4]);
      px[0] = rand() % 256; // Valore casuale per il rosso (R)
      px[1] = rand() % 256; // Valore casuale per il verde (G)
      px[2] = rand() % 256; // Valore casuale per il blu (B)
      px[3] = rand() % 256; // Valore casuale per il canale alfa
    }
  }
}

// Funzione principale
int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Enter output filename\n");
    exit(EXIT_FAILURE);
  }

  srand(time(NULL));       // Inizializza il generatore di numeri casuali
  create_random_image();   // Crea l'immagine con pixel randomici
  write_png_file(argv[1]); // Scrive l'immagine su file

  return 0;
}
