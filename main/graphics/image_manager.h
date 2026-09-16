#ifndef IMAGE_MANAGER_H
#define IMAGE_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Precaricamento e disegno dei loghi
void preload_miniLogo();
void draw_miniLogoTopRight(int logoW = 60, int logoH = 47, int offsetX = 10, int offsetY = 2);
void preload_logo();                                    // aggiunta
void drawLogoAtCenter(int logoW = 150, int logoH = 114); // valori di default

void preload_hemispherical_image(void);
void draw_hemispherical_image(int x, int y, int imgW, int imgH);

// Precaricamento immagini per i tab
void preloadTabThreadImages(void);
void preloadTabTaperMorseImages(void);

// Disegno immagini per i tab
void draw_tabThreadImage(int x, int y, int w, int h, int type);
void draw_tabTaperMorseImage(int x, int y, int w, int h, int type);

#ifdef __cplusplus
}
#endif

#endif