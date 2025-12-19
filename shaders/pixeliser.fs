#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

uniform float renderWidth;
uniform float renderHeight;
uniform float time;

vec2 transformCurve(vec2 uv) {
  uv -= 0.5;
  float r = (uv.x * uv.x + uv.y * uv.y) * 0.65;
  uv *= 4.1 + r;
  uv *= 0.25;
  uv += 0.5;
  return uv;
}

vec4 refreshLines(vec4 color, vec2 uv)
{
    uv.y = 1 - uv.y;
    float loopTime = 8.0;
    float front = fract(time / loopTime);
    float tailLength = 0.5;
    float lineThickness = 0.003;
    float line = step(abs(uv.y - front), lineThickness);
    float tail = 0.0;
    float d = uv.y - front;
    d = mod(d + 1.0, 1.0);
    if (d <= tailLength) {
        tail = 1.0 - (d / tailLength);
    }
    vec3 tailColor  = vec3(0.3, 0.6, 1.0);

    color.rgb += tail * (tailColor * 0.01);
    return clamp(color.rgba, 0, 1);
}

float noise(vec2 co){
    //return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453 + time);
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    vec3 bluePhosphor = vec3(0.4, 0.8, 1.2);

    vec2 uv = transformCurve(fragTexCoord);
    vec4 outsideColor = vec4(0.788, 0.780, 0.685, 1);
    vec4 borderColor = vec4(0.1, 0.1, 0.1, 1);

    if(uv.x <  -0.025 || uv.y <  -0.025) {
        finalColor = outsideColor;
        return;
    }
    if(uv.x >   1.025 || uv.y >   1.025) {
        finalColor = outsideColor;
        return;
    }
    // Bezel
    if(uv.x <  -0.015 || uv.y <  -0.015) {
        finalColor = borderColor;
        return;
    }
    if(uv.x >   1.015 || uv.y >   1.015) {
        finalColor = borderColor;
        return;
    }
    // Screen Border
    if(uv.x <  -0.001 || uv.y <  -0.001) {
        finalColor = vec4(0, 0, 0, 1);
        return;
    }
    if(uv.x >   1.001 || uv.y >   1.001) {
        finalColor = vec4(0, 0, 0, 1);
        return;
    }

    vec4 texelColor = texture(texture0, fragTexCoord);
    if (texelColor != vec4(0, 0, 0, 1) && texelColor != vec4(1, 1, 1, 1)) {
        finalColor = texelColor;
        return;
    }

    float bgGlowIntensity = 1;
    float bgGlow = bgGlowIntensity * smoothstep(1.2, 0.2, length(fragTexCoord - 0.5));
    vec3 backgroundColor = bluePhosphor * bgGlow * 0.4 * mix(0.40, 0.55, noise(fragTexCoord));
    vec3 color = backgroundColor + (bluePhosphor * texelColor.rgb);

    vec4 endColor = vec4(color, 1);
    endColor = refreshLines(endColor, fragTexCoord);

    finalColor = endColor;
}

