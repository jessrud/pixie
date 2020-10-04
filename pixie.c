#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIX_SS 1000
#define OP_SS 1000
#define WMAXLEN 100

#define RED(p) ((uint8_t)(p >> 16))
#define GREEN(p) ((uint8_t)(p >> 8))
#define BLUE(p) ((uint8_t)p)

// rotational shift operators //////////////////////////////////////////////////

#define rot24l(num, amt)                                                       \
    (((num << (amt % 24)) | (num >> (24 - (amt % 24)))) & 0x00FFFFFF)

#define rot24r(num, amt)                                                       \
    (((num >> (amt % 24)) | (num << (24 - (amt % 24)))) & 0x00FFFFFF)

#define isalpha(c) ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))

#define dinfo(s, ...) fprintf(stderr, s, ##__VA_ARGS__)

// helpers for dealing with the oper stack /////////////////////////////////////

#define pushOp(oper) opStack[++opTop].op = oper

#define topOp opStack[opTop]
#define botOp opStack[opPtr]
#define popOp() opStack[opTop--]

// helpers for dealing with the pixel stack ////////////////////////////////////

#define pushPix(p)                                                             \
    ({                                                                         \
        if (pixTop > PIX_SS - 1) {                                             \
            return E_EOVERFLOW;                                                \
        } else {                                                               \
            pixStack[++pixTop] = (p & 0x00FFFFFF);                             \
        }                                                                      \
    })

#define popPix(x)                                                              \
    ({                                                                         \
        if (pixTop < 0) {                                                      \
            pixTop = -1;                                                       \
            return E_EUNDERFLOW;                                               \
        } else {                                                               \
            *(x) = pixStack[pixTop--];                                         \
        }                                                                      \
    })

////////////////////////////////////////////////////////////////////////////////
// opcodes grouped by the number of values they need on the top of the stack
// to function correctly.
////////////////////////////////////////////////////////////////////////////////

typedef enum { O_NOOP, O_RAND, O_PUSH, AFTEROP0 } OP0;

typedef enum { O_POP = AFTEROP0, O_DUP, O_LOAD, AFTEROP1 } OP1;

typedef enum {
    O_ROTR = AFTEROP1,
    O_ROTL,
    O_AND,
    O_OR,
    O_ADD,
    O_SUB,
    O_MUL,
    O_DIV,
    O_SWAP,
    O_STORE,
    OPCOUNT
} OP2;

char const *mnem[OPCOUNT] = {
    [O_NOOP] = "noop", [O_ROTR] = "rotr",   [O_ROTL] = "rotl",
    [O_AND] = "and",   [O_OR] = "or",       [O_ADD] = "add",
    [O_SUB] = "sub",   [O_RAND] = "rand",   [O_MUL] = "mul",
    [O_DIV] = "div",   [O_POP] = "pop",     [O_DUP] = "dup",
    [O_SWAP] = "swap", [O_STORE] = "store", [O_LOAD] = "load"};

// generic structures and types ////////////////////////////////////////////////

typedef int OPCODE;
typedef uint32_t Pixel;

typedef struct {
    OPCODE op;
    Pixel pixel;
} Oper;

// machine state ///////////////////////////////////////////////////////////////

Oper opStack[OP_SS];
ssize_t opTop = -1;
size_t opPtr = 0;

Pixel pixStack[OP_SS];
size_t width = 80;
size_t height = 80;
uint32_t depth = 255;
ssize_t pixTop = -1;

bool debug = false;

// parser state ////////////////////////////////////////////////////////////////

int lastChar;
Pixel workingPixel;
char lastWord[WMAXLEN + 1];

// Error/Return codes //////////////////////////////////////////////////////////

#define OK 0

// execution error codes
typedef enum {
    E_EOVERFLOW = -500,
    E_EUNDERFLOW,
    E_EINVALIDOP,
} E_RES;

// parsing error codes
typedef enum {
    P_EOVERWORD = -400,
    P_EOVEROP,
    P_EUNDERLIT,
    P_EBADLIT,
    P_EBADDIMS,
    P_EBADWORD,
} P_RES;

// generic error codes
typedef int G_RES;

// helpers for debugging the state of a running script
void debugOpStack();
void debugPixStack();

// header generation & parsing ////////////////////////////////////////////////

// generate ppm header
void header() {
    printf("P3\n");
    printf("%zu %zu\n", width, height);
    printf("%d", depth);
}

// parse script header
P_RES parseHead() {
    int count = scanf("%zu %zu\n", &width, &height);
    if (count != 2)
        return P_EBADDIMS;
    return OK;
}

// body parsing functions and helpers //////////////////////////////////////////

void advance() { lastChar = getchar(); }

#define isBreak                                                                \
    ((lastChar == ';') || (lastChar == ' ') || (lastChar == '\n') ||           \
     (lastChar == '\t') || (lastChar == '\r'))
#define isEOF (lastChar == EOF)

#define MAXPIXEL ((Pixel)0xFFFFFF)

#define baseShift(v)                                                           \
    (workingPixel = (workingPixel * base), workingPixel += lastWord[i] - v)

// parse numerical literals
P_RES parseLit(bool push) {
    workingPixel = 0;
    uint8_t base;

    // where to start looking for digits
    int start = 2;

    // determine bits per digit
    switch (lastWord[1]) {
    case 0:
        return P_EUNDERLIT;
    case 'B':
    case 'b':
        base = 2;
        break;
    case 'X':
    case 'x':
        base = 16;
        break;
    default:
        base = 10;
        start = 1;
    }

    // upper bounds on digit characters
    int maxLower = 'a' + (base - 11);
    int maxCapital = 'A' + (base - 11);
    int maxDigit = base > 10 ? '9' : ('0' + (base - 1));

    for (int i = start;; i++) {

        if (lastWord[i] == 0) {
            if (i == start)
                return P_EUNDERLIT;

            workingPixel &= MAXPIXEL;

            if (push) {
                pushOp(O_PUSH);
                topOp.pixel = workingPixel;
            }
            return OK;
        }

        if (base > 10) {
            if (lastWord[i] >= 'a' && lastWord[i] <= maxLower) {
                baseShift('a' + 10);
                continue;

            } else if (lastWord[i] >= 'A' && lastWord[i] <= maxCapital) {
                baseShift('A' + 10);
                continue;
            }
        }
        if (lastWord[i] >= '0' && lastWord[i] <= maxDigit) {
            baseShift('0');
            continue;
        }
        return P_EBADLIT;
    }

    // discards whitespace and comments
    void discardBreak() {
        bool inComment = false;
        for (;; advance()) {
            if isEOF
                return;
            if (inComment) {
                inComment = !(lastChar == '\n');
                continue;
            }
            inComment = lastChar == ';';
            if isBreak
                continue;
            return;
        }
    }

    // parse contiguous characters seperated by whitespace and comments.
    P_RES parseWord() {
        discardBreak();
        int i = 0;
        lastWord[0] = 0;
        if isEOF
            return EOF;

        for (; (!isBreak) && (!isEOF); (i++, advance())) {
            if (i == WMAXLEN)
                return P_EOVERWORD;
            lastWord[i] = lastChar;
        }
        lastWord[i] = 0;
        return OK;
    }

    // takes the text of the last word parsed and tries to put a corresponding
    // opcode on the stack. if the opcode is unrecognized, returns 'P_EBADWORD'
    P_RES parseOp() {
        {
            P_RES res = parseWord();
            if (res != OK)
                return res;
        }

        // Check if special
        switch (lastWord[0]) {
        case '#':
            return parseLit(true);
        }
        // Try all opcodes
        for (OPCODE op = 0; op < OPCOUNT; op++) {

            if (mnem[op] == 0)
                continue; // non-mnemonic operation

            if (!strcmp(mnem[op], lastWord)) {
                pushOp(op);
                return OK;
            }
        }
        return P_EBADWORD;
    }

    // iterate through the body of the script trying to applying the opcode
    // parser
    P_RES parseBody() {
        advance();
        P_RES res;
        do
            res = parseOp();
        while (res == OK);

        if (res == EOF)
            return OK;
        return res;
    }

    // compute the pixel at a give offset by executing all instructions
    // on the opStack FIFO
    E_RES pixel(size_t row, size_t col) {
        opPtr = 0;

        // push column and row to the pixel stack
        pushPix((Pixel)row);
        pushPix((Pixel)col);

        // debugPixStack();

        while ((ssize_t)opPtr <= opTop) {
            if (debug) {
                debugPixStack();
                debugOpStack();
            }
            Oper theOp = botOp;
            opPtr++;

            // nullary (0 argument) operations:
            switch ((OP0)theOp.op) {
            case O_PUSH:
                pushPix(theOp.pixel);
                continue;
            case O_RAND:
                pushPix(rand());
                continue;
            case O_NOOP:
                continue;
            case AFTEROP0:; // warnings w/o
            }

            // 1 arity operators:
            Pixel oper1;
            popPix(&oper1);

            switch ((OP1)theOp.op) {
            case O_DUP:
                pushPix(oper1);
                pushPix(oper1);
                continue;
            case O_POP:
                continue;
            case O_LOAD:
                pushPix(pixStack[oper1]);
                continue;
            case AFTEROP1:; // warnings w/o
            }

            // 2 arity operators:
            Pixel oper2;
            popPix(&oper2);

            switch ((OP2)theOp.op) {
            case O_ADD:
                pushPix(oper1 + oper2);
                continue;

            case O_SUB:
                pushPix(oper1 - oper2);
                continue;

            case O_MUL:
                pushPix(oper1 * oper2);
                continue;

            case O_DIV:
                pushPix(oper1 / oper2);
                continue;

            case O_SWAP:
                pushPix(oper1);
                pushPix(oper2);
                continue;

            case O_AND:
                pushPix(oper1 & oper2);
                continue;

            case O_OR:
                pushPix(oper1 | oper2);
                continue;

            case O_ROTL:
                pushPix(rot24l(oper2, oper1));
                continue;

            case O_ROTR:
                pushPix(rot24r(oper2, oper1));
                continue;

            case O_STORE:
                pixStack[oper1] = oper2;
                continue;

            case OPCOUNT:
                return E_EINVALIDOP;
            }
            return E_EINVALIDOP;
        }
        if (debug)
            debugPixStack();
        debug = false;
        Pixel pix;
        popPix(&pix);

        printf("%03hhu %03hhu %03hhu\t\t", RED(pix), GREEN(pix), BLUE(pix));
        return OK;
    }

    // parse the list of initialization values just below the dimension header
    G_RES parseInit() {
        advance();
        G_RES res;

        for (;;) {
            res = parseWord();
            if (res != OK)
                return res;

            switch (lastWord[0]) {
            case '#':
                res = parseLit(false);
                if (res != OK)
                    return res;
                pushPix(workingPixel);
                continue;
            case '-':
                return OK;
            default:
                dinfo("%s\n", lastWord);
                dinfo("lastChar: %c\n", lastChar);
                return P_EBADWORD;
            }
        }
    }

    // debugging functions /////////////////////////////////////////////////////
    void debugOpStack() {
        dinfo(" %ld opStack: ", opTop + 1);

        for (int i = 0; i <= opTop; i++) {
            if (i == (int)opPtr)
                dinfo("[");
            Pixel pix;
            OPCODE op = opStack[i].op;

            if (op == O_PUSH) {
                pix = opStack[i].pixel;
                dinfo("%02X%02X%02X", RED(pix), GREEN(pix), BLUE(pix));

            } else if (mnem[op] == 0) {
                dinfo("<invalid>");

            } else
                dinfo(mnem[op]);

            if (i == (int)opPtr)
                dinfo("]");
            dinfo(" ");
        }
        dinfo("\n");
    }

    void debugPixStack() {
        dinfo(" %ld pixStack: ", pixTop + 1);

        for (int i = pixTop; i >= 0; i--)
            dinfo("%06X ", pixStack[i]);

        dinfo("\n");
    }

    //////////////// ///////////////////////////////////////////////////////////

    int main() {

        // prints a debug trace of the generation of a single
        // pixel
        if (getenv("PIXIE_DEBUG") != NULL)
            debug = true;

        P_RES perr;
        if ((perr = parseHead()) != OK) {
            dinfo("Invalid header!\n");
            return -1;
        }
        if ((perr = parseInit()) != OK) {
            dinfo("Initialization error: ");
            switch ((int)perr) {
            case EOF:
                dinfo("encountered end of file before body");
                break;
            case E_EOVERFLOW:
                dinfo("pixel stack overflow!\n");
                break;
            case P_EBADWORD:
                dinfo("only numerical literals are valid in header");
                break;
            default:
                dinfo("malformed pixel literal in initialization section\n");
            }
            return -1;
        }

        // parse body of script
        if ((perr = parseBody()) != OK) {
            dinfo("Parse error: ");
            switch (perr) {
            case P_EOVEROP:
                dinfo("Instruction stack overflow!\n");
                break;
            case P_EOVERWORD:
                dinfo("word length limit exceeded!\n");
                break;
            case P_EBADLIT:
                dinfo("invalid character in pixel literal\n");
                break;
            case P_EUNDERLIT:
                dinfo("bad pixel literal; no digits!\n");
                break;
            case P_EBADWORD:
                dinfo("unrecognized word!\n");
                break;
            default:
                dinfo("encountered unknown error while parsing!\n");
            }
            return -1;
        }

        header();

        // iterate through pixel indices and apply the program
        // to each.
        for (size_t r = 0, i = 0; r < height; r++)
            for (size_t c = 0; c < width; (c++, i++)) {

                // break after 5 pixels. this currently keeps the output
                // within a width of 80 characters.
                if ((i % 5) == 0)
                    putc('\n', stdout);

                E_RES e;
                if ((e = pixel(r, c)) != OK) {
                    switch (e) {
                    case E_EOVERFLOW:
                        dinfo("Pixel stack overflow!\n");
                        break;
                    case E_EUNDERFLOW:
                        dinfo("Pixel stack underflow!\n");
                        break;
                    case E_EINVALIDOP:
                        dinfo("Encountered invalid instruction!\n");
                        break;
                    default:
                        dinfo("Unknown error. bug.\n");
                    }
                    debugOpStack();
                    debugPixStack();
                    return -1;
                }
            }
    }
