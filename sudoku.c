// Copyright 2012 Philip Puryear
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

typedef struct {
    int** cells;
    int size, num_cells;
} Puzzle;

typedef struct DLXObject {
    struct DLXObject *left, *right, *up, *down, *column;
    void* data;
} DLXObject;

typedef struct {
    int row, column, value;
} DLXRowData;

typedef struct {
    int row_count;
} DLXColumnData;

typedef struct {
    bool print_solutions;
    bool print_num_solutions;
} ProgramConfig;

typedef struct {
    DLXObject* header;
    Puzzle* init;
    Puzzle* solution;
    uint64_t num_solutions;
} DLXParameters;

static const int kMaxPuzzleSize = 256;
static ProgramConfig config = {
    .print_solutions = true,
    .print_num_solutions = false
};
static DLXParameters dlx_params;

static void fatal(const char* msg, ...) {
    va_list ap;
    fprintf(stderr, "error: ");
    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

static void* xmalloc(size_t n) {
    void* p = malloc(n);
    if (!p)
        fatal("out of memory");
    return p;
}

static void print_puzzle(Puzzle* solution, Puzzle* init) {
    int size = solution->size;
    int max_width = 1 + (int) log10(size);
    bool highlight_solved_cells = init && isatty(STDOUT_FILENO);
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            if (j > 0)
                printf(" ");

            int value = solution->cells[i][j];
            if (value == 0)
                printf(".");
            else {
                bool highlight_cell =
                    highlight_solved_cells && init->cells[i][j] == 0;

                if (highlight_cell)
                    printf("\x1b[1m");
                printf("%*d", max_width, value);
                if (highlight_cell)
                    printf("\x1b[0m");
            }
        }
        printf("\n");
    }
}

static void init_puzzle(Puzzle* p, int size) {
    p->cells = xmalloc(sizeof(int*) * size);
    int num_cells = size * size;
    p->cells[0] = xmalloc(sizeof(int) * num_cells);
    for (int i = 1; i < size; i++)
        p->cells[i] = p->cells[i - 1] + size;
    p->size = size;
    p->num_cells = num_cells;
}

static void copy_puzzle(Puzzle* a, Puzzle* b) {
    init_puzzle(a, b->size);
    memcpy(a->cells[0], b->cells[0], sizeof(int) * b->num_cells);
}

static void free_puzzle(Puzzle* puzzle) {
    free(puzzle->cells[0]);
    free(puzzle->cells);
}

static inline bool issquare(int x) {
    int s = sqrt(x);
    return s * s == x;
}

static int read_puzzle(Puzzle* puzzle, FILE* in) {
    bool read_size = false;
    puzzle->cells = NULL;
    int cell = 0;
    while (!feof(in)) {
        // Read in one field at a time, ignoring all whitespace.
        // A field can never be longer than the maximum length of the
        // decimal representation of an int (10 digits).
        char buf[16];
        // The trailing space is necessary to consume all trailing whitepace.
        if (fscanf(in, "%15s ", buf) != 1)
            goto err;
        if (!read_size) {
            // Interpret the first field as the puzzle size.
            int size;
            if (sscanf(buf, "%d", &size) != 1 || size < 1 ||
                size > kMaxPuzzleSize || !issquare(size))
                goto err;

            init_puzzle(puzzle, size);
            read_size = true;
            continue;
        }

        if (cell >= puzzle->num_cells)
            goto err;
        int value;
        if (strcmp(buf, ".") == 0)
            value = 0;
        else if (sscanf(buf, "%d", &value) != 1 ||
                 value < 1 || value > puzzle->size)
            goto err;
        puzzle->cells[0][cell] = value;
        cell++;
    }
    if (read_size && cell == puzzle->num_cells)
        return 0;
err:
    if (puzzle->cells)
        free_puzzle(puzzle);
    return -1;
}

static inline DLXColumnData* get_column_data(DLXObject* c) {
    return (DLXColumnData*) c->data;
}

static void cover_column(DLXObject* c) {
    c->right->left = c->left;
    c->left->right = c->right;
    for (DLXObject* i = c->down; i != c; i = i->down) {
        for (DLXObject* j = i->right; j != i; j = j->right) {
            j->down->up = j->up;
            j->up->down = j->down;
            get_column_data(j->column)->row_count--;
        }
    }
}

static void uncover_column(DLXObject* c) {
    for (DLXObject* i = c->up; i != c; i = i->up) {
        for (DLXObject* j = i->left; j != i; j = j->left) {
            get_column_data(j->column)->row_count++;
            j->down->up = j;
            j->up->down = j;
        }
    }
    c->right->left = c;
    c->left->right = c;
}

static void dlx_solve(void) {
    DLXObject* h = dlx_params.header;
    if (h->right == h) {
        // Found a solution.
        if (config.print_solutions) {
            if (dlx_params.num_solutions > 0)
                printf("\n");
            print_puzzle(dlx_params.solution, dlx_params.init);
        }
        dlx_params.num_solutions++;
        return;
    }

    DLXObject* c = h;
    int min_row_count = dlx_params.solution->size + 1;
    for (DLXObject* j = h->right; j != h; j = j->right) {
        int row_count = get_column_data(j)->row_count;
        if (row_count < min_row_count) {
            c = j;
            min_row_count = row_count;
        }
    }
    cover_column(c);
    for (DLXObject* r = c->down; r != c; r = r->down) {
        // Record this value in the solution puzzle.
        DLXRowData row_data = *(DLXRowData*) r->data;
        dlx_params.solution->cells[row_data.row][row_data.column] =
            row_data.value;

        for (DLXObject* j = r->right; j != r; j = j->right)
            cover_column(j->column);
        dlx_solve();

        for (DLXObject* j = r->left; j != r; j = j->left)
            uncover_column(j->column);
    }
    uncover_column(c);
}

static void solve_puzzle(Puzzle* p) {
    // Manufacture a DLX structure for solving p, then call dlx_solve().
    int puzzle_size = p->size;
    int num_cells = p->num_cells;
    int num_constraints = 4 * num_cells;
    int num_choices = num_cells * puzzle_size;

    // Allocate all DLXObjects at once, then link them together below.
    DLXObject* header = xmalloc(sizeof(DLXObject) *
            (num_choices * 4 + num_constraints + 1));

    // Link up the column headers (which correspond to constraints).
    DLXObject* last_constraint = header + num_constraints;
    header->left = last_constraint;
    header->right = header + 1;
    DLXColumnData* col_data = xmalloc(sizeof(DLXColumnData) * num_constraints);
    // Keep a list of the bottom-most object in each column.
    DLXObject** object_cols = xmalloc(sizeof(DLXObject*) * num_constraints);
    for (int i = 0; i < num_constraints; i++) {
        DLXObject* c = header + 1 + i;
        c->left = c - 1;
        c->right = c + 1;
        c->column = c;
        c->data = &col_data[i];
        col_data[i].row_count = puzzle_size;
        object_cols[i] = c;
    }
    last_constraint->right = header;

    // Link up the row objects.
    DLXObject* obj = last_constraint + 1;
    DLXRowData* row_data_start = xmalloc(sizeof(DLXRowData) * num_choices);
    DLXRowData* row_data = row_data_start;
    int block_size = sqrt(puzzle_size);
    for (int r = 0; r < puzzle_size; r++) {
        for (int c = 0; c < puzzle_size; c++) {
            int block_index = r - r % block_size + c / block_size;
            for (int v = 1; v < puzzle_size + 1; v++) {
                DLXRowData* data = row_data++;
                data->row = r;
                data->column = c;
                data->value = v;

                DLXObject* start = obj;
                // Calculate the index (i.e. distance from header) of the four
                // constraint columns corresponding to this (row, col, value)
                // triple.
                int constraint_indices[4] = {
                    puzzle_size * r + c,               // row-column
                    puzzle_size * r + v - 1,           // row-value
                    puzzle_size * c + v - 1,           // column-value
                    puzzle_size * block_index + v - 1  // block-value
                };
                for (int i = 0; i < 4; i++) {
                    int idx = i * num_cells + constraint_indices[i];
                    DLXObject* obj_above = object_cols[idx];
                    obj->up = obj_above;
                    obj->left = obj - 1;
                    obj->right = obj + 1;
                    obj->column = header + 1 + idx;
                    obj->data = data;
                    obj_above->down = obj;
                    object_cols[idx] = obj;
                    obj++;
                }
                start->left = obj - 1;
                (obj - 1)->right = start;
            }
        }
    }
    for (int i = 0; i < num_constraints; i++) {
        DLXObject* o = object_cols[i];
        o->down = o->column;
        o->column->up = o;
    }
    free(object_cols);

    // Prepare the DLX structure according to our puzzle's initial values.
    for (int r = 0; r < puzzle_size; r++) {
        for (int c = 0; c < puzzle_size; c++) {
            int v = p->cells[r][c];
            if (v != 0) {
                DLXObject* dlx_row = last_constraint + 1 +
                        4 * (num_cells * r + puzzle_size * c + v - 1);

                cover_column(dlx_row->column);
                for (DLXObject* o = dlx_row->right; o != dlx_row; o = o->right)
                    cover_column(o->column);
            }
        }
    }

    Puzzle solution;
    copy_puzzle(&solution, p);
    dlx_params = (DLXParameters) {
        .header = header,
        .init = p,
        .solution = &solution,
        .num_solutions = 0
    };
    dlx_solve();

    if (config.print_num_solutions)
        printf("%" PRIu64 "\n", dlx_params.num_solutions);
    else if (dlx_params.num_solutions == 0)
        printf("Puzzle has no solutions.\n");

    free_puzzle(&solution);
    free(row_data_start);
    free(col_data);
    free(header);
}

static void print_usage(void) {
    printf(
"usage: sudoku [OPTIONS] PUZZLE_FILE\n"
"\n"
"Options:\n"
"  -n    print only the number of solutions found\n"
"  -h    show this message and exit\n");
}

int main(int argc, char** argv) {
    static const struct option long_options[] = {
        { "number-only", no_argument, NULL, 'n' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };
    for (;;) {
        int c = getopt_long(argc, argv, "nh", long_options, NULL);
        if (c == -1)
            break;

        switch (c) {
        case 'n':
            config.print_num_solutions = true;
            config.print_solutions = false;
            break;
        case 'h':
            print_usage();
            return EXIT_SUCCESS;
        default:
            return EXIT_FAILURE;
        }
    }
    argv += optind;
    argc -= optind;
    if (argc < 1)
        fatal("not enough arguments");
    if (argc > 1)
        fatal("too many arguments");

    Puzzle p;
    FILE* f = fopen(argv[0], "r");
    if (!f)
        fatal("cannot open %s: %s", argv[0], strerror(errno));
    if (read_puzzle(&p, f) < 0) {
        const char* error_str = ferror(f) ? strerror(errno) :
            "incorrect puzzle format";
        fatal("error reading %s: %s", argv[0], error_str);
    }

    solve_puzzle(&p);
    free_puzzle(&p);
    fclose(f);
    return 0;
}
