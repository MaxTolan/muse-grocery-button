/*
 * Host tests for the grocery button's conversation logic: transcript parsing,
 * the script (docs/03-conversation-design.md), the outbox, and the speech
 * API helpers. Build and run with tools/test.sh.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grocery_outbox.h"
#include "grocery_parse.h"
#include "grocery_script.h"
#include "grocery_source.h"
#include "grocery_speech_util.h"

static int s_failed, s_run;

#define CHECK(cond)                                                              \
    do {                                                                         \
        s_run++;                                                                 \
        if (!(cond)) {                                                           \
            s_failed++;                                                          \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define CHECK_STR(a, b)                                                                  \
    do {                                                                                 \
        s_run++;                                                                         \
        if (strcmp((a), (b))) {                                                          \
            s_failed++;                                                                  \
            fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
        }                                                                                \
    } while (0)

/* ---- parsing ---- */

static void parse_case(const char *text, grocery_store_t store, const char *item, int ends)
{
    grocery_heard_t h;
    grocery_parse(text, &h);
    if (h.store != store || strcmp(h.item, item) || (ends >= 0 && h.ends_session != ends)) {
        s_failed++;
        fprintf(stderr, "parse(\"%s\"): store %d item \"%s\" ends %d; wanted %d \"%s\" %d\n", text, h.store,
                h.item, h.ends_session, store, item, ends);
    }
    s_run++;
}

static void test_parse(void)
{
    parse_case("Fairlife skim milk, 52 ounce.", GROCERY_STORE_NONE, "Fairlife skim milk, 52 ounce", 0);
    parse_case("eggs", GROCERY_STORE_NONE, "eggs", 0);
    parse_case("Costco milk", GROCERY_STORE_COSTCO, "milk", 0);
    parse_case("milk from Costco", GROCERY_STORE_COSTCO, "milk", 0);
    parse_case("add paper towels to the Costco list please", GROCERY_STORE_COSTCO, "paper towels", 0);
    parse_case("Put bananas on the Kroger list.", GROCERY_STORE_KROGER, "bananas", 0);
    parse_case("we're out of coffee at Kroger", GROCERY_STORE_KROGER, "coffee", 0);
    parse_case("Kruger bread", GROCERY_STORE_KROGER, "bread", 0);
    parse_case("Kroger's sourdough", GROCERY_STORE_KROGER, "sourdough", 0);
    parse_case("cost co toilet paper", GROCERY_STORE_COSTCO, "toilet paper", 0);
    parse_case("Costco.", GROCERY_STORE_COSTCO, "", 0);
    parse_case("Kroger", GROCERY_STORE_KROGER, "", 0);
    parse_case("eggs from Costco or Kroger", GROCERY_STORE_NONE, "eggs or", 0);
    parse_case("I need eggs", GROCERY_STORE_NONE, "eggs", 0);
    parse_case("Can you add Greek yogurt", GROCERY_STORE_NONE, "Greek yogurt", 0);
    parse_case("get", GROCERY_STORE_NONE, "get", 0);   /* never trims the whole thing away */
    parse_case("That's it.", GROCERY_STORE_NONE, "That's it", 1);
    parse_case("that\xE2\x80\x99s all", GROCERY_STORE_NONE, "that\xE2\x80\x99s all", 1);
    parse_case("No, that's it, thanks.", GROCERY_STORE_NONE, "No, that's it", 1);
    parse_case("Nope.", GROCERY_STORE_NONE, "Nope", 1);
    parse_case("no thank you", GROCERY_STORE_NONE, "no", 1);   /* the item is unused once it ends */
    parse_case("nothing else", GROCERY_STORE_NONE, "nothing else", 1);
    parse_case("I'm done", GROCERY_STORE_NONE, "I'm done", 1);
    parse_case("no frills butter", GROCERY_STORE_NONE, "no frills butter", 0);
    parse_case("donuts", GROCERY_STORE_NONE, "donuts", 0);
    parse_case("", GROCERY_STORE_NONE, "", 0);

    grocery_heard_t h;
    grocery_parse("...", &h);
    CHECK(!h.has_words);
    grocery_parse("jalape\xC3\xB1os", &h);
    CHECK(h.has_words);
    CHECK_STR(h.item, "jalape\xC3\xB1os");
    CHECK_STR(grocery_store_name(GROCERY_STORE_COSTCO), "Costco");
    CHECK_STR(grocery_store_name(GROCERY_STORE_NONE), "");

    /* A long transcript is cut on a character boundary. */
    char longtext[400];
    memset(longtext, 0, sizeof(longtext));
    for (int i = 0; i < 60; i++) {
        strcat(longtext, "\xC3\xB1\xC3\xB1 ");
    }
    grocery_parse(longtext, &h);
    CHECK(strlen(h.item) < GROCERY_ITEM_MAX);
    size_t n = strlen(h.item);
    CHECK(n > 0 && ((unsigned char)h.item[n - 1] & 0xC0) == 0x80);   /* ends on a whole character, not a space or lead byte */
}

/* ---- the script ---- */

static grocery_action_t step(grocery_script_t *s, grocery_event_t ev, const char *text)
{
    grocery_action_t a;
    grocery_script_step(s, ev, text, &a);
    return a;
}

static void test_happy_path(void)
{
    grocery_script_t s;
    grocery_script_start(&s);
    grocery_action_t a = step(&s, GROCERY_EV_HEARD, "Fairlife skim milk at Kroger");
    CHECK_STR(a.say, "Added Fairlife skim milk to Kroger. Anything else?");
    CHECK(a.submit && a.store == GROCERY_STORE_KROGER);
    CHECK_STR(a.item, "Fairlife skim milk");
    CHECK(a.view == GROCERY_VIEW_ADDED && a.view_store == GROCERY_STORE_KROGER);
    CHECK(!a.end && !a.answer_ms);

    a = step(&s, GROCERY_EV_HEARD, "That's it.");
    CHECK_STR(a.say, "Got it.");
    CHECK(a.end && !a.submit && a.view == GROCERY_VIEW_DONE);

    /* Letting go without speaking after an item ends it too. */
    grocery_script_start(&s);
    step(&s, GROCERY_EV_HEARD, "Costco eggs");
    a = step(&s, GROCERY_EV_SILENT, NULL);
    CHECK_STR(a.say, "Got it.");
    CHECK(a.end);

    /* So does a single short press. */
    grocery_script_start(&s);
    a = step(&s, GROCERY_EV_TAP, NULL);
    CHECK_STR(a.say, "Got it.");
    CHECK(a.end);
}

static void test_store_question(void)
{
    grocery_script_t s;
    grocery_script_start(&s);
    grocery_action_t a = step(&s, GROCERY_EV_HEARD, "Eggs.");
    CHECK_STR(a.say, "Costco or Kroger?");
    CHECK(!a.submit && a.answer_ms == GROCERY_ANSWER_MS);
    CHECK(a.view == GROCERY_VIEW_ASK_STORE);
    CHECK_STR(a.view_item, "Eggs");

    a = step(&s, GROCERY_EV_HEARD, "Costco.");
    CHECK_STR(a.say, "Added Eggs to Costco. Anything else?");
    CHECK(a.submit && a.store == GROCERY_STORE_COSTCO);
    CHECK_STR(a.item, "Eggs");
    CHECK(!a.answer_ms);

    /* Unanswered: ask once more, then Kroger. */
    grocery_script_start(&s);
    step(&s, GROCERY_EV_HEARD, "eggs");
    a = step(&s, GROCERY_EV_NO_ANSWER, NULL);
    CHECK_STR(a.say, "Sorry \xE2\x80\x94 Costco or Kroger?");
    CHECK(a.answer_ms && !a.submit);
    a = step(&s, GROCERY_EV_NO_ANSWER, NULL);
    CHECK_STR(a.say, "I'll put eggs on the Kroger list.");
    CHECK(a.submit && a.store == GROCERY_STORE_KROGER && a.end);

    /* An answer that isn't a store counts as no answer; then Kroger, session still open. */
    grocery_script_start(&s);
    step(&s, GROCERY_EV_HEARD, "eggs");
    a = step(&s, GROCERY_EV_HEARD, "um the big one");
    CHECK_STR(a.say, "Sorry \xE2\x80\x94 Costco or Kroger?");
    a = step(&s, GROCERY_EV_SILENT, NULL);
    CHECK_STR(a.say, "I'll put eggs on the Kroger list.");
    CHECK(a.submit && !a.end);

    /* Ending while an item waits for its store files it to Kroger. */
    grocery_script_start(&s);
    step(&s, GROCERY_EV_HEARD, "eggs");
    a = step(&s, GROCERY_EV_TAP, NULL);
    CHECK_STR(a.say, "I'll put eggs on the Kroger list.");
    CHECK(a.submit && a.end);
    grocery_script_start(&s);
    step(&s, GROCERY_EV_HEARD, "eggs");
    a = step(&s, GROCERY_EV_HEARD, "that's it");
    CHECK(a.submit && a.store == GROCERY_STORE_KROGER && a.end);

    /* Too long while waiting for a store keeps the question open. */
    grocery_script_start(&s);
    step(&s, GROCERY_EV_HEARD, "eggs");
    a = step(&s, GROCERY_EV_TOO_LONG, NULL);
    CHECK_STR(a.say, "That was a bit long \xE2\x80\x94 try again?");
    CHECK(a.answer_ms && !a.submit);
    a = step(&s, GROCERY_EV_HEARD, "Kroger");
    CHECK(a.submit && a.store == GROCERY_STORE_KROGER);
    CHECK_STR(a.item, "eggs");
}

static void test_misses(void)
{
    grocery_script_t s;
    grocery_script_start(&s);
    grocery_action_t a = step(&s, GROCERY_EV_HEARD, "");
    CHECK_STR(a.say, "I didn't catch that \xE2\x80\x94 try again?");
    CHECK(!a.end && a.view == GROCERY_VIEW_RETRY);
    a = step(&s, GROCERY_EV_HEARD, "...");
    CHECK_STR(a.say, "Let's try once more, nice and slow.");
    CHECK(a.end);

    /* A good item in between resets the count. */
    grocery_script_start(&s);
    step(&s, GROCERY_EV_HEARD, "");
    step(&s, GROCERY_EV_HEARD, "milk at Kroger");
    a = step(&s, GROCERY_EV_HEARD, "");
    CHECK(!a.end);

    /* Silent on the first press is a miss, not an ending. */
    grocery_script_start(&s);
    a = step(&s, GROCERY_EV_SILENT, NULL);
    CHECK(!a.end && a.view == GROCERY_VIEW_RETRY);

    /* Only a store name, with no item. */
    grocery_script_start(&s);
    a = step(&s, GROCERY_EV_HEARD, "Costco");
    CHECK(a.view == GROCERY_VIEW_RETRY && !a.submit);

    /* Offline. */
    grocery_script_start(&s);
    a = step(&s, GROCERY_EV_OFFLINE, NULL);
    CHECK(a.end && a.view == GROCERY_VIEW_OFFLINE && !a.submit);
    grocery_script_start(&s);
    step(&s, GROCERY_EV_HEARD, "eggs");
    a = step(&s, GROCERY_EV_OFFLINE, NULL);
    CHECK(a.end && a.submit && a.store == GROCERY_STORE_KROGER);

    /* NO_ANSWER with nothing pending does nothing. */
    grocery_script_start(&s);
    a = step(&s, GROCERY_EV_NO_ANSWER, NULL);
    CHECK(!a.say[0] && !a.end && !a.submit);
}

/* ---- the outbox ---- */

static void test_outbox(void)
{
    grocery_outbox_t ob;
    grocery_outbox_init(&ob);
    char msg[GROCERY_MESSAGE_MAX];
    CHECK(grocery_outbox_message(&ob, msg, sizeof(msg)) == 0);
    CHECK(!grocery_outbox_add(&ob, "", GROCERY_STORE_KROGER));
    CHECK(grocery_outbox_add(&ob, "Fairlife skim milk, 52 ounce", GROCERY_STORE_KROGER));
    CHECK(grocery_outbox_message(&ob, msg, sizeof(msg)) == 1);
    CHECK_STR(msg, "[fridge-button] Please put this on my Costco/Kroger grocery lists.\n- Kroger: Fairlife skim milk, 52 ounce");
    CHECK(grocery_outbox_add(&ob, "eggs\tand\nham", GROCERY_STORE_COSTCO));
    CHECK(grocery_outbox_message(&ob, msg, sizeof(msg)) == 2);
    CHECK(strstr(msg, "these") && strstr(msg, "\n- Costco: eggs and ham"));

    char blob[GROCERY_BLOB_MAX];
    grocery_outbox_save(&ob, blob, sizeof(blob));
    CHECK_STR(blob, "K\tFairlife skim milk, 52 ounce\nC\teggs and ham\n");
    grocery_outbox_t back;
    grocery_outbox_load(&back, blob);
    CHECK(back.count == 2 && back.e[1].store == GROCERY_STORE_COSTCO);
    CHECK_STR(back.e[1].item, "eggs and ham");
    grocery_outbox_load(&back, "junk\nC\t\nK\tbread");
    CHECK(back.count == 1);
    CHECK_STR(back.e[0].item, "bread");

    /* Issue #2: the tag opens every message, exactly, and never lands in an item. */
    CHECK(!strncmp(msg, GROCERY_SOURCE_TAG " ", strlen(GROCERY_SOURCE_TAG) + 1));
    CHECK(strstr(msg + 1, GROCERY_SOURCE_TAG) == NULL);
    CHECK(strstr(blob, GROCERY_SOURCE_TAG) == NULL);
    CHECK_STR(grocery_untagged("[fridge-button] Added eggs to Kroger."), "Added eggs to Kroger.");
    CHECK_STR(grocery_untagged("[fridge-button]: Added eggs."), "Added eggs.");
    CHECK_STR(grocery_untagged("Added eggs."), "Added eggs.");
    CHECK(grocery_untagged(NULL) == NULL);

    /* Delivery of the first n leaves anything added since. */
    int n = grocery_outbox_message(&ob, msg, sizeof(msg));
    grocery_outbox_add(&ob, "butter", GROCERY_STORE_KROGER);
    grocery_outbox_delivered(&ob, n);
    CHECK(ob.count == 1);
    CHECK_STR(ob.e[0].item, "butter");

    /* Full, and a message that doesn't fit its buffer covers fewer items. */
    grocery_outbox_init(&ob);
    for (int i = 0; i < GROCERY_OUTBOX_MAX; i++) {
        CHECK(grocery_outbox_add(&ob, "a fairly long grocery item name for testing", GROCERY_STORE_KROGER));
    }
    CHECK(!grocery_outbox_add(&ob, "one too many", GROCERY_STORE_KROGER));
    CHECK(grocery_outbox_message(&ob, msg, sizeof(msg)) == GROCERY_OUTBOX_MAX);
    char small[200];
    int k = grocery_outbox_message(&ob, small, sizeof(small));
    CHECK(k > 0 && k < GROCERY_OUTBOX_MAX && strlen(small) < sizeof(small));
    CHECK(!strncmp(small, GROCERY_SOURCE_TAG " ", strlen(GROCERY_SOURCE_TAG) + 1));   /* split batches too */

    /* Items are cut on a character boundary. */
    char wide[300] = "";
    for (int i = 0; i < 100; i++) {
        strcat(wide, "\xC3\xB1");
    }
    grocery_outbox_init(&ob);
    grocery_outbox_add(&ob, wide, GROCERY_STORE_KROGER);
    size_t len = strlen(ob.e[0].item);
    CHECK(len % 2 == 0 && len < GROCERY_ITEM_MAX);
}

/* ---- speech helpers ---- */

static void test_speech_util(void)
{
    const char *cases[] = { "", "f", "fo", "foo", "foob", "fooba", "foobar" };
    const char *want[] = { "", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy" };
    for (int i = 0; i < 7; i++) {
        char out[16];
        size_t n = grocery_b64_encode((const uint8_t *)cases[i], strlen(cases[i]), out);
        out[n] = '\0';
        CHECK_STR(out, want[i]);
        CHECK(n == GROCERY_B64_LEN(strlen(cases[i])));
        uint8_t back[16];
        size_t m = grocery_b64_decode(out, n, back);
        CHECK(m == strlen(cases[i]) && !memcmp(back, cases[i], m));
    }
    uint8_t buf[8];
    CHECK(grocery_b64_decode("Zm9v\nYmFy", 9, buf) == 6 && !memcmp(buf, "foobar", 6));
    CHECK(grocery_b64_decode("Pz\\/=", 5, buf) == 2 && buf[0] == 0x3f && buf[1] == 0x3f);

    char esc[64];
    CHECK(grocery_json_escape("say \"hi\"\n\\", esc, sizeof(esc)));
    CHECK_STR(esc, "say \\\"hi\\\"\\n\\\\");
    CHECK(!grocery_json_escape("toolong", esc, 4));

    char text[256];
    float conf;
    CHECK(grocery_stt_transcript("{\"results\": [{\"alternatives\": [{\"transcript\": \"Fairlife skim milk\","
                                 "\"confidence\": 0.92}, {\"transcript\": \"fair life\"}],"
                                 "\"resultEndTime\": \"1.5s\"}, {\"alternatives\": [{\"transcript\": \" at Kroger\","
                                 "\"confidence\": 0.5}]}], \"totalBilledTime\": \"2s\"}",
                                 text, sizeof(text), &conf));
    CHECK_STR(text, "Fairlife skim milk at Kroger");
    CHECK(fabsf(conf - 0.92f) < 0.001f);
    CHECK(grocery_stt_transcript("{\"totalBilledTime\": \"1s\"}", text, sizeof(text), &conf));
    CHECK_STR(text, "");
    CHECK(conf == 0);
    CHECK(grocery_stt_transcript("{\"results\":[{\"alternatives\":[{\"transcript\":\"caf\\u00e9 \\\"crema\\\"\"}]}]}",
                                 text, sizeof(text), &conf));
    CHECK_STR(text, "caf\xC3\xA9 \"crema\"");
    CHECK(!grocery_stt_transcript("{\"error\": {\"code\": 400, \"message\": \"API key not valid.\"}}", text,
                                  sizeof(text), &conf));
    CHECK(grocery_api_error("{\"error\": {\"code\": 400, \"message\": \"API key not valid.\"}}", text, sizeof(text)));
    CHECK_STR(text, "API key not valid.");
    CHECK(!grocery_stt_transcript("<html>", text, sizeof(text), &conf));

    size_t len;
    const char *v = grocery_json_string("{\"audioContent\": \"UklG\\/RgAA\"}", "audioContent", &len);
    CHECK(v && len == 10 && !strncmp(v, "UklG\\/RgAA", len));
    CHECK(!grocery_json_string("{\"audio\": 1}", "audioContent", &len));

    /* A minimal WAV: fmt, an extra chunk, then data. */
    uint8_t wav[64] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E',
                        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x80, 0x3e, 0, 0, 0, 0x7d, 0, 0, 2, 0, 16, 0,
                        'L', 'I', 'S', 'T', 2, 0, 0, 0, 'x', 'y',
                        'd', 'a', 't', 'a', 8, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    size_t off, plen;
    uint32_t rate;
    CHECK(grocery_wav_pcm(wav, 62, &off, &plen, &rate));
    CHECK(off == 54 && plen == 8 && rate == 16000);
    CHECK(grocery_wav_pcm(wav, 58, &off, &plen, &rate) && plen == 4);   /* truncated data: what's there */
    wav[22] = 2;   /* stereo */
    CHECK(!grocery_wav_pcm(wav, 62, &off, &plen, &rate));
    CHECK(!grocery_wav_pcm((const uint8_t *)"nope", 4, &off, &plen, &rate));
    uint8_t bad[24] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'J', 'U', 'N', 'K', 0xff, 0xff, 0xff, 0xff };
    CHECK(!grocery_wav_pcm(bad, sizeof(bad), &off, &plen, &rate));
}

int main(void)
{
    test_parse();
    test_happy_path();
    test_store_question();
    test_misses();
    test_outbox();
    test_speech_util();
    printf("%d checks, %d failed\n", s_run, s_failed);
    return s_failed ? 1 : 0;
}
