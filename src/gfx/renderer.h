#pragma once

#include <SDL.h>
#include <cstdint>
#include <string>
#include <vector>
#include "bitmap.h"

namespace op2 {

class Renderer {
public:
    static constexpr int NATIVE_WIDTH = 640;
    static constexpr int NATIVE_HEIGHT = 480;

    Renderer();
    ~Renderer();

    bool init(const std::string& title, int windowWidth = 640, int windowHeight = 480, bool fullscreen = false);
    // Tela cheia sem troca de modo de video (FULLSCREEN_DESKTOP): a vista de
    // 640x480 e escalada mantendo a proporcao, com barras onde sobrar.
    void setFullscreen(bool on);
    bool fullscreen() const;
    void toggleFullscreen() { setFullscreen(!fullscreen()); }
    void shutdown();

    void clear(uint32_t color = 0xFF000000);
    void present();

    // Drawing primitives directly on the 640x480 native buffer
    void blitBitmap(const Bitmap& bmp, int destX, int destY, bool useColorKey = false, uint32_t colorKey = 0xFF000000);
    // Blit com opacidade (alpha 0..255; 255 = opaco). Mistura com o fundo.
    void blitBitmapAlpha(const Bitmap& bmp, int destX, int destY, uint8_t alpha,
                         bool useColorKey = false, uint32_t colorKey = 0xFF000000);
    void blitBitmapSub(const Bitmap& bmp, int srcX, int srcY, int srcW, int srcH, int destX, int destY);

    void fillRect(int x, int y, int w, int h, uint32_t color);
    // Escurece o que já está desenhado (alpha 0..255; 255 = preto). O fillRect
    // grava a cor crua, então não serve para velar o fundo.
    void dimRect(int x, int y, int w, int h, uint8_t amount);
    void drawRect(int x, int y, int w, int h, uint32_t color);
    void drawBevelRect(int x, int y, int w, int h, uint32_t topLeftColor, uint32_t bottomRightColor);

    // Dialog / Window frame (classic Outpost 2 dark metallic beveled box)
    // Preenchimento MISTURADO com o que ja esta no quadro: opacity 255 pinta
    // por cima (o mesmo que fillRect), 0 nao pinta nada. E assim que os
    // quadros translucidos deixam a arte do fundo aparecer -- fillRect com
    // alpha na cor nao serve, porque grava a cor crua.
    void fillRectAlpha(int x, int y, int w, int h, uint32_t color, uint8_t opacity);
    // `opacity` < 255 deixa o fundo do quadro (e a barra de titulo)
    // translucidos; as bordas e o texto continuam opacos, para nao borrar.
    void drawDialogFrame(int x, int y, int w, int h, const std::string& title = "",
                         uint8_t opacity = 255);
    void drawButton(int x, int y, int w, int h, const std::string& text, bool hovered,
                    bool pressed, bool disabled = false, uint8_t opacity = 255);

    // Fork: controls of the skinned shell dialogs (see DialogSkin), as the
    // original menus look on Windows 10. Without the skin they draw the port's
    // old dark style, so callers need no fallback.
    // List / edit / combo body: black, with a 2 px light border.
    void drawSkinBox(int x, int y, int w, int h);
    // Vertical scroll bar: arrow buttons at both ends, track and thumb.
    void drawSkinScrollBar(int x, int y, int w, int h, int first, int visible, int total);
    // A scroll-bar style button with an up or down arrow (also the combo's).
    void drawSkinArrowButton(int x, int y, int w, int h, bool up);
    // Radio / check glyph (13x13) at (x, y).
    void drawSkinRadio(int x, int y, bool on, bool disabled);
    void drawSkinCheck(int x, int y, bool on, bool disabled);

    uint32_t* getFramebuffer() { return m_framebuffer.data(); }
    int getWidth() const { return m_w; }
    int getHeight() const { return m_h; }

    // Tamanho logico do quadro (o shell fica em 640x480 escalado; o jogo usa o
    // tamanho da janela 1:1). Devolve true se mudou -- o conteudo e apagado.
    bool setLogicalSize(int w, int h);
    // Quadro do tamanho da janela, com um minimo. Devolve true se mudou.
    bool fitToWindow(int minW, int minH);
    // Deslocamento subtraido das coordenadas do mouse (dialogo de 640x480
    // desenhado centralizado num quadro maior).
    void setInputOffset(int dx, int dy) { m_inputOffX = dx; m_inputOffY = dy; }

    SDL_Window* getWindow() const { return m_window; }

    // Map screen coordinate to native 640x480 coordinate
    void screenToNative(int screenX, int screenY, int& nativeX, int& nativeY) const;
    // O caminho inverso, para os testes poderem posicionar um clique de
    // verdade: dado o ponto no quadro, onde ele cai na JANELA.
    void nativeToScreen(int nativeX, int nativeY, int& screenX, int& screenY) const {
        if (m_w <= 0 || m_h <= 0) { screenX = screenY = 0; return; }
        screenX = m_viewportRect.x + (nativeX + m_inputOffX) * m_viewportRect.w / m_w;
        screenY = m_viewportRect.y + (nativeY + m_inputOffY) * m_viewportRect.h / m_h;
    }

private:
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_sdlRenderer = nullptr;
    SDL_Texture* m_nativeTexture = nullptr;

    std::vector<uint32_t> m_framebuffer;
    int m_w = NATIVE_WIDTH, m_h = NATIVE_HEIGHT;
    int m_inputOffX = 0, m_inputOffY = 0;
    SDL_Rect m_viewportRect = {0, 0, NATIVE_WIDTH, NATIVE_HEIGHT};

    void updateViewport();
};

} // namespace op2
