#pragma once

#include <string>
#include <SFML/Graphics.hpp>


class GuiElement {
public:
    float x = 0, y = 0;
    float xParent = 0, yParent = 0;
    float w = 0, h = 0;
    std::vector<GuiElement*> childGuiElements;

    sf::Font *font = nullptr;
    static sf::Font staticFont;

    bool isVisible = true;

    static void prepare() {
        staticFont.loadFromFile("data/fonts/fixedsys/Fixedsys.ttf");
    }
    virtual ~GuiElement() {
        for(int i=0; i<childGuiElements.size(); i++) {
            delete childGuiElements[i];
        }
    }

    GuiElement() {
        this->font = &staticFont;
    }

    void updateTree() {
        if(!isVisible) {
            return;
        }
        for(int i=0; i<childGuiElements.size(); i++) {
            childGuiElements[i]->xParent = xParent + x;
            childGuiElements[i]->yParent = yParent + y;
            childGuiElements[i]->updateTree();
        }
    }

    void addChild(GuiElement *childGuiElement) {
        childGuiElements.push_back(childGuiElement);
    }

    void renderTree(sf::RenderWindow &window) {
        if(!isVisible) {
            return;
        }

        render(window);
        for(int i=0; i<childGuiElements.size(); i++) {
            childGuiElements[i]->renderTree(window);
        }
    }

    virtual void render(sf::RenderWindow &window) {}

    void setSize(float w, float h) {
        if(this->w == w && this->h == h) {
            return;
        }

        this->w = w;
        this->h = h;

        onSetSize();
    }

    void loadFontFromFile(const std::string &filename) {
        if(font) {
            delete font;
        }
        font = new sf::Font();
        font->loadFromFile(filename);

        onFontLoaded();
    }

    bool isPointWithin(float px, float py) {
        float x = this->x + xParent;
        float y = this->y + yParent;
        return px >= x && px <= x+w && py >= y && py <= y+h;
    }

    virtual void onFontLoaded() {}
    virtual void onSetSize() {}
    virtual void onMouseEntered() {}
    virtual void onMouseExit() {}
    virtual void onMouseClicked() {}

    bool mouseWithin = false;

    bool isMouseWithinTree(float mx, float my) {
        if(!isVisible) {
            return false;
        }

        bool ret = false;

        for(int i=0; i<childGuiElements.size(); i++) {
            ret = ret || childGuiElements[i]->isMouseWithinTree(mx, my);
        }
        
        bool b = isPointWithin(mx, my);
        
        if(b && !mouseWithin) {
            onMouseEntered();
            mouseWithin = true;
        }
        else if(!b && mouseWithin) {
            onMouseExit();
            mouseWithin = false;
        }

        ret = ret || b;
        
        return ret;
    }

    bool mouseClicked(float mx, float my) {
        if(!isVisible) {
            return false;
        }

        bool ret = false;

        for(int i=0; i<childGuiElements.size(); i++) {
            ret = ret || childGuiElements[i]->mouseClicked(mx, my);
        }

        if(!ret && mouseWithin) {
            onMouseClicked();
            ret = true;
        }

        return ret;
    }

};

sf::Font GuiElement::staticFont;










class Panel : public GuiElement {

protected:
    std::string title = "";
    sf::Text titleText;
    float titleTextSize = 64;
    float titleTextW = 0, titleTextH = 0;
    sf::RectangleShape bgRect;
    sf::RectangleShape borderRect;


public:
    bool moveable = false;
    float borderThickness = 3.0;
    float borderGaps = 4.0;
    float titleGaps = 10.0;

    float titleX = 50;
    float titleY = 0;
    
    float internalTitleX = 0;
    float internalTitleY = 0;

    Panel() {
        GuiElement();

        bgRect.setFillColor(sf::Color(0, 0, 0, 100));
        bgRect.setOutlineColor(sf::Color(0, 0, 0, 100));
        bgRect.setOutlineThickness(0);

        borderRect.setFillColor(sf::Color(255, 255, 255, 255));
        borderRect.setOutlineColor(sf::Color(255, 255, 255, 255));
        borderRect.setOutlineThickness(0);
        

        setSize(300, 300);
        setTitle("<no title>");
    }

    virtual void onFontLoaded() {
        titleText.setFont(*font);
        setTitle(title);
    }

    /*virtual void onMouseEntered() {
        setTitle("Mouse Enter!");
    }
    virtual void onMouseExit() {
        setTitle("Mouse Exit!");
    }*/

    void setTitle(const std::string &title) {
        this->title = title;
        titleText.setString(title);
        titleText.setFont(*font);
        titleText.setCharacterSize(titleTextSize);
        //titleText.setOrigin(-10, 10);
        //titleText.setLineSpacing(0);

        sf::FloatRect fr = titleText.getLocalBounds();
        titleTextW = fr.width;
        //titleTextH = fr.height;
        titleTextH = titleTextSize;
        //internalTitleX = fr.left;
        //internalTitleY = fr.top;

        //printf("title: %s, rect: %f, %f, %f, %f\n", title.c_str(), fr.left, fr.top, fr.width, fr.height);

        borderGaps = (titleTextH - borderThickness) * 0.5;
    }


    virtual void render(sf::RenderWindow &window) {

        float x = this->x + xParent;
        float y = this->y + yParent;

        if(title.size() == 0) {

        }
        else {
            bgRect.setPosition(x, y);
            window.draw(bgRect);

            //titleText.setPosition(x+borderGaps+titleX-internalTitleX, y+titleY-internalTitleY-titleTextSize/4);
            titleText.setPosition(x+borderGaps+titleX, y+titleY-titleTextSize/4);
            window.draw(titleText);

            { // rendering borders
                float xLeftTop = borderGaps;
                float xLeftOfTitleTop = borderGaps + titleX - titleGaps;
                float xRightOfTitleTop = borderGaps + titleX + titleTextW + titleGaps*2.0;
                float xRightTop = w - borderGaps - borderThickness;
                float yTop = borderGaps + titleY;

                borderRect.setPosition(x + xLeftTop, y + yTop);
                float borderW = xLeftOfTitleTop - xLeftTop;
                borderRect.setSize(sf::Vector2f(borderW, borderThickness));
                window.draw(borderRect);

                borderRect.setPosition(x + xRightOfTitleTop, y + yTop);
                borderW = xRightTop - xRightOfTitleTop;
                borderRect.setSize(sf::Vector2f(borderW, borderThickness));
                window.draw(borderRect);

                float xLeftBottom = xLeftTop;
                float yBottom = h - borderGaps - borderThickness;
                borderRect.setPosition(x + xLeftTop, y + yTop);
                float borderH = yBottom - yTop;
                borderRect.setSize(sf::Vector2f(borderThickness, borderH));
                window.draw(borderRect);

                float xRightBottom = xRightTop;
                borderRect.setPosition(x + xRightTop, y + yTop);
                borderH = yBottom - yTop;
                borderRect.setSize(sf::Vector2f(borderThickness, borderH));
                window.draw(borderRect);

                borderRect.setPosition(x + xLeftBottom, y + yBottom);
                borderW = xRightBottom - xLeftBottom + borderThickness;
                borderRect.setSize(sf::Vector2f(borderW, borderThickness));
                window.draw(borderRect);

            }
        }
    }

    virtual void onSetSize() {
        bgRect.setSize(sf::Vector2f(w, h));
    }
};






class Button : public GuiElement {

protected:
    std::string title = "";
    sf::Text titleText;
    float titleTextSize = 36;
    float titleTextW = 0, titleTextH = 0;
    sf::RectangleShape bgRectWithPossibleOtlineBorder;
    //sf::RectangleShape borderRect;
    sf::Color activeColor;
    sf::Color inactiveColor;

public:
    bool moveable = false;
    float borderThickness = 3.0;


    Button() {
        GuiElement();

        activeColor = sf::Color(255, 255, 255, 100);
        inactiveColor = sf::Color(0, 0, 0, 100);

        bgRectWithPossibleOtlineBorder.setFillColor(inactiveColor);
        bgRectWithPossibleOtlineBorder.setOutlineColor(sf::Color(255, 255, 255, 100));
        bgRectWithPossibleOtlineBorder.setOutlineThickness(borderThickness);
        

        setSize(300, 300);
        setTitle("<no title>");

        titleText.setFillColor(sf::Color::White);
    }

    virtual void onFontLoaded() {
        titleText.setFont(*font);
        setTitle(title);
    }

    virtual void onMouseEntered() {
        bgRectWithPossibleOtlineBorder.setFillColor(activeColor);
    }
    virtual void onMouseExit() {
        bgRectWithPossibleOtlineBorder.setFillColor(inactiveColor);
    }

    void setTitle(const std::string &title) {
        this->title = title;
        titleText.setString(title);
        titleText.setFont(*font);
        titleText.setCharacterSize(titleTextSize);
        //titleText.setOrigin(-10, 10);
        //titleText.setLineSpacing(0);

        sf::FloatRect fr = titleText.getLocalBounds();
        titleTextW = fr.width;
        titleTextH = titleTextSize;


        //setSize(titleTextW+8, titleTextH+8);
    }


    virtual void render(sf::RenderWindow &window) {

        float x = this->x + xParent;
        float y = this->y + yParent;

        bgRectWithPossibleOtlineBorder.setPosition(x, y);
        window.draw(bgRectWithPossibleOtlineBorder);

        //titleText.setPosition(x+borderGaps+titleX-internalTitleX, y+titleY-internalTitleY-titleTextSize/4);
        float dx = w * 0.5 - titleTextW * 0.5;
        float dy = h * 0.5 - titleTextH * 0.5 - titleTextSize/4;

        titleText.setPosition(x+dx, y+dy);
        window.draw(titleText);
    }

    virtual void onSetSize() {
        bgRectWithPossibleOtlineBorder.setSize(sf::Vector2f(w, h));
    }
};