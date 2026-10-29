#include "hw/xbox/hle/shader/ffhw.h"
#include "hle_test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char buf[65536];

static void dump(const char *name)
{
    const char *dir = getenv("HLE_TEST_DUMP");
    if (dir) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.vert", dir, name);
        FILE *f = fopen(path, "wb");
        if (f) {
            fputs(buf, f);
            fclose(f);
        }
    }
}

static size_t gen(const HleFfhwKey *k, uint32_t in_mask)
{
    uint32_t tok[HLE_FFHW_SLOTS * 4];
    memcpy(tok, k, sizeof(*k));
    HleVshKey vk = { .in_mask = in_mask };
    return hle_ffhw_glsl(tok, HLE_FFHW_SLOTS, &vk, buf, sizeof(buf));
}

static void test_unlit(void)
{
    HleFfhwKey k = { .magic = HLE_FFHW_MAGIC, .specular = 1, .separate_spec = 1,
                     .alpha_mat_spec = 1 };
    size_t n = gen(&k, 1u << 0 | 1u << 3 | 1u << 9);
    HT_CHECK(n > 0 && n == strlen(buf));
    dump("ffhw_unlit");
    /* object space through the composite, over w, plus the viewport offset */
    HT_CHECK(strstr(buf, "vec4 sp = XF(pos, 0);") != NULL);
    HT_CHECK(strstr(buf, "oPos = vec4(sp.xyz / sw + c[68].xyz, sw);") != NULL);
    HT_CHECK(strstr(buf, "oD0 = v3;") != NULL);
    HT_CHECK(strstr(buf, "oT0 = v9;") != NULL);
    HT_CHECK(strstr(buf, "vec4 v1 = vcur[1];") != NULL);   /* the program head's */
    HT_CHECK(strstr(buf, "vFog = 1.0;") != NULL);          /* no fog written */
    HT_CHECK(strstr(buf, "gl_Position") != NULL);
}

static void test_lit_skinned(void)
{
    HleFfhwKey k = { .magic = HLE_FFHW_MAGIC, .skin = 3, .lighting = 1, .normalize = 1,
                     .specular = 1, .fog = 3, .src = { 0, 1, 1, 0 },
                     .light = { 1, 2, 3 } };
    k.texgen[1][0] = HLE_FFHW_TG_SPHERE;
    k.texgen[1][1] = HLE_FFHW_TG_SPHERE;
    k.texgen[2][2] = HLE_FFHW_TG_REFLECTION;
    k.texmat = 1u << 1;
    size_t n = gen(&k, 0x1E1F);
    HT_CHECK(n > 0);
    dump("ffhw_lit");
    /* 3G: two weights given, the third generated */
    HT_CHECK(strstr(buf, "wi = v1.x;\n    wn -= wi;") != NULL);
    HT_CHECK(strstr(buf, "wi = v1.y;\n    wn -= wi;") != NULL);
    HT_CHECK(strstr(buf, "wi = wn;\n    tPos += XF(pos, 12) * wi;") != NULL);
    HT_CHECK(strstr(buf, "tN += XF(n4, 28).xyz * wi;") != NULL);
    HT_CHECK(strstr(buf, "tN = normalize(tN);") != NULL);
    HT_CHECK(strstr(buf, "vec4 sp = XF(tPos, 0);") != NULL);   /* skinned: eye space */
    /* ambient from the diffuse register, scaled by the emission register */
    HT_CHECK(strstr(buf, "oD0 = vec4(v3.rgb * c[72].rgb + c[71].rgb, v3.a);") != NULL);
    HT_CHECK(strstr(buf, "/* light 0 */") && strstr(buf, "/* light 1 */") &&
             strstr(buf, "/* light 2 */") && !strstr(buf, "/* light 3 */"));
    HT_CHECK(strstr(buf, "nh = max(dot(tN, c[80].xyz), 0.0);") != NULL);  /* infinite: half */
    HT_CHECK(strstr(buf, "lit = d <= c[89].x;") != NULL);               /* local: range */
    HT_CHECK(strstr(buf, "vec4 sd = c[103];") != NULL);                 /* spot */
    HT_CHECK(strstr(buf, "oD0.rgb += v3.rgb * c[77].rgb * (att * nl);") != NULL);
    HT_CHECK(strstr(buf, "oD0.rgb += oD1.rgb;") != NULL);   /* not separate */
    HT_CHECK(strstr(buf, "oT1.x = r.x / (2.0 * length(r + vec3(0.0, 0.0, 1.0))) + 0.5;"));
    HT_CHECK(strstr(buf, "oT2.z = reflect(normalize(tPos.xyz), tN).z;") != NULL);
    HT_CHECK(strstr(buf, "oT1 = XF(oT1, 40);") != NULL);
    HT_CHECK(strstr(buf, "oFog = vec4(dot(c[69].xyz, tPos.xyz) + c[69].w);") != NULL);
    HT_CHECK(strstr(buf, "vFog = oFog.x;") != NULL);
}

/* two-sided lighting, as the NV2A tests (Lighting Two Sided, Specular back)
 * show it: lit, the back colours from the back registers along -N by the
 * back's own colour sources, and without SEPARATE_SPECULAR the front vertex
 * specular passed through; unlit, (0, 0, 0, 1) whatever the back colour
 * registers hold */
static void test_two_side(void)
{
    HleFfhwKey k = { .magic = HLE_FFHW_MAGIC, .lighting = 1, .two_side = 1, .specular = 1,
                     .src = { 0, 0, 1, 0 }, .bsrc = { 0, 0, 0, 2 }, .light = { 1, 2 } };
    size_t n = gen(&k, 0x1E1F | 3u << 7);               /* back colour arrays too */
    HT_CHECK(n > 0 && n == strlen(buf));
    dump("ffhw_two_side");
    /* the front's diffuse from the vertex, the back's from the material */
    HT_CHECK(strstr(buf, "oD0 = vec4(c[71].rgb * c[72].rgb + c[71].rgb, v3.a);") != NULL);
    HT_CHECK(strstr(buf, "oB0 = vec4(c[156].rgb * c[157].rgb + c[156].rgb, c[156].w);") != NULL);
    HT_CHECK(strstr(buf, "oD0.rgb += v3.rgb * c[77].rgb * (att * nl);") != NULL);
    HT_CHECK(strstr(buf, "oB0.rgb += vec3(1.0) * c[162].rgb * (att * bl);") != NULL);
    HT_CHECK(strstr(buf, "oB1.rgb += v4.rgb * c[163].rgb * (att * bf);") != NULL);
    HT_CHECK(strstr(buf, "float bl = max(dot(-tN, Ld), 0.0), "
                         "bh = max(dot(-tN, c[80].xyz), 0.0);") != NULL);
    HT_CHECK(strstr(buf, "float bl = max(dot(-tN, VP), 0.0), "
                         "bh = max(dot(-tN, normalize(VP + eyeV)), 0.0);") != NULL);
    HT_CHECK(strstr(buf, "pow(bh, c[73].y)") != NULL);           /* the back power */
    /* no SEPARATE_SPECULAR: the front vertex specular, back arrays or not */
    HT_CHECK(strstr(buf, "oB0.rgb += oB1.rgb;\n    oB1 = v4;") != NULL);
    HT_CHECK(strstr(buf, "oB1 = v8") == NULL && strstr(buf, "oB0 = v7") == NULL);
    /* unlit: black, alpha 1, whatever the back colour registers hold */
    k.lighting = 0;
    HT_CHECK(gen(&k, 0x1E1F | 3u << 7) > 0);
    HT_CHECK(strstr(buf, "oB0 = vec4(0.0, 0.0, 0.0, 1.0);\n"
                         "    oB1 = vec4(0.0, 0.0, 0.0, 1.0);") != NULL);
    HT_CHECK(strstr(buf, "oB1 = v4") == NULL);
    /* off: as before (the pixel stage doesn't read them) */
    k.two_side = 0;
    HT_CHECK(gen(&k, 0x1E1F) > 0);
    HT_CHECK(strstr(buf, "oB0 = v7;\n    oB1 = v8;") != NULL);
}

static void test_not_a_key(void)
{
    uint32_t tok[12] = { 0 };
    HleVshKey vk = { 0 };
    HT_CHECK(hle_ffhw_glsl(tok, 3, &vk, buf, sizeof(buf)) == 0);
    HT_CHECK(!hle_ffhw_is(tok, 3));
    tok[0] = HLE_FFHW_MAGIC;
    HT_CHECK(!hle_ffhw_is(tok, 2));
    HT_CHECK(hle_ffhw_is(tok, 3));
}

static void test_specular_power(void)
{
    /* a first coefficient of 0 is a power of 1 (the first fit's constant);
     * positive ones are not D3D's */
    const float p1[6] = { -0.0f, 0.0f, 1.0f, -0.0f, 0.0f, 1.0f };
    HT_CHECK(fabsf(hle_ffhw_specular_power(p1) - 1.0f) < 0.05f);
    const float none[6] = { 0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f };
    HT_CHECK(hle_ffhw_specular_power(none) < 0.001f);
}

int main(void)
{
    HT_RUN(test_unlit);
    HT_RUN(test_lit_skinned);
    HT_RUN(test_two_side);
    HT_RUN(test_not_a_key);
    HT_RUN(test_specular_power);
    HT_DONE();
}
