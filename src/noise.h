#pragma once
#include <ctgmath>


struct Noise {
public:
    float getNoise(float x, float y) {
        return noise(x, y);
    }
    float getNoise(float x, float y, float a, float b) {
        return noise(x, y) * (b-a) + a;
    }
    int getNoisei(float x, float y, int a, int b) {
        return (int)(noise(x, y) * (b+0.99999-a) + a);
    }

    float getFBM(float x, float y, int octaves, float amplitude, float frequency) {
        return fbm(x, y, octaves, amplitude, frequency);
    }
    float getFBMi(float x, float y, int octaves, float amplitude, float frequency, int a, int b) {
        float r = fbm(x, y, octaves, amplitude, frequency);
        return (int)(r * (b+0.99999-a) + a);
    }

private:
    float dot(float ax, float ay, float bx, float by) {
        return ax * bx + ay * by;
    }
    float fract(float v) {
        return v - floor(v);
    }
    float random(float x, float y) {
        return fract(sin(dot(x, y, 12.9898, 78.233)) * 4758.5453123);
    }
    float smoothstep(float a, float b, float x) {
        // Scale, bias and saturate x to 0..1 range
        //x = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
        x = (x - a) / (b - a);
        if(x < 0) x = 0;
        else if(x > 1) x = 1;
        // Evaluate polynomial
        return x * x * (3 - 2 * x);
    }
    float mix(float x, float y, float a) {
        return (1.0-a) * x + a * y;
    }

    float noise(float x, float y) {
        float xi = floor(x);
        float yi = floor(y);
        float xf = x - xi;
        float yf = y - yi;

        float a = random(xi, yi);
        float b = random(xi+1, yi);
        float c = random(xi, yi+1);
        float d = random(xi+1, yi+1);

        float xb = smoothstep(0, 1, xf);
        float yb = smoothstep(0, 1, yf);

        return mix(a, b, xb) + (c-a)*yb * (1.0-xb) + (d-b)*xb*yb;
    }

    float fbm(float x, float y, int octaves, float amplitude, float frequency) {
        float value = 0;
        for(int i=0; i<octaves; i++) {
            value += amplitude * noise(frequency*x, frequency*y);
            amplitude *= 0.5;
            frequency *= 2.0;
        }
        return value;
    }
};
