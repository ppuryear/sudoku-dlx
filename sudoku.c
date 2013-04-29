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
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

typedef struct {
    int** cells;
    int size;
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
    bool print_attempts;
} ProgramConfig;

typedef struct {
    DLXObject* header;
    Puzzle* solution;
    uint64_t num_solutions;
} DLXParameters;

static const int kMaxPuzzleSize = 256;
static ProgramConfig config;
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

static void print_puzzle(Puzzle* p) {
    int size = p->size;
    int max_width = log10(size);
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            int value = p->cells[i][j];
            if (value == 0)
                printf(".");
            else
                printf("%*d", max_width, value);
            if (j != size - 1)
                printf(" ");
        }
        printf("\n");
    }
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
    puzzle->cells = NULL;
    puzzle->size = 0;
    int cell = 0, num_cells = 0;
    while (!feof(in)) {
        // Read in one field at a time, ignoring all whitespace.
        // A field can never be longer than the maximum length of the
        // decimal representation of an int (10 digits).
        char buf[16];
        // The trailing space is necessary to consume all trailing whitepace.
        if (fscanf(in, "%15s ", buf) != 1)
            goto err;
        if (puzzle->size == 0) {
            // Interpret the first field as the puzzle size.
            int size;
            if (sscanf(buf, "%d", &size) != 1 || size < 1 ||
                size > kMaxPuzzleSize || !issquare(size))
                goto err;

            puzzle->cells = xmalloc(sizeof(int*) * size);
            num_cells = size * size;
            // Allocate all cells at once, then slice up the allocation.
            puzzle->cells[0] = xmalloc(sizeof(int) * num_cells);
            for (int i = 1; i < size; i++)
                puzzle->cells[i] = puzzle->cells[i - 1] + size;
            puzzle->size = size;
            continue;
        }

        if (cell >= num_cells)
            goto err;
        int value;
        if (strcmp(buf, ".") == 0)
            value = 0;
        else {
            if (sscanf(buf, "%d", &value) != 1 || value < 1 ||
                value > puzzle->size)
                goto err;
        }
        puzzle->cells[0][cell] = value;
        cell++;
    }
    if (puzzle->size > 0 && cell == num_cells)
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

static void dlx_solve(int k) {
    DLXObject* h = dlx_params.header;
    Puzzle* p = dlx_params.solution;
    if (h->right == h) {
        // Found a solution.
        if (config.print_solutions) {
            print_puzzle(p);
            printf("\n");
        }
        dlx_params.num_solutions++;
        return;
    }

    DLXObject* c = h;
    int min_row_count = p->size + 1;
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
        if (config.print_attempts)
            printf("[%d] Trying %d at (%d,%d).\n",
                    k, row_data.value, row_data.row, row_data.column);
        p->cells[row_data.row][row_data.column] = row_data.value;

        for (DLXObject* j = r->right; j != r; j = j->right)
            cover_column(j->column);
        dlx_solve(k + 1);

        for (DLXObject* j = r->left; j != r; j = j->left)
            uncover_column(j->column);
    }
    uncover_column(c);
}

static void solve_puzzle(Puzzle* p) {
    // Manufacture a DLX structure for solving p, then call dlx_solve().
    int puzzle_size = p->size;
    int num_cells = puzzle_size * puzzle_size;
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

    dlx_params = (DLXParameters) {
        .header = header,
        .solution = p,
        .num_solutions = 0
    };
    dlx_solve(0);

    if (config.print_num_solutions)
        printf("%zu\n", dlx_params.num_solutions);
    else if (dlx_params.num_solutions == 0)
        printf("Puzzle has no solutions.\n");
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
"  -v    print every attemped cell value\n"
"  -h    show this message and exit\n");
}

int main(int argc, char** argv) {
    config = (ProgramConfig) {
        .print_solutions = true,
        .print_num_solutions = false,
        .print_attempts = false
    };
    static const struct option long_options[] = {
        { "number-only", no_argument, NULL, 'n' },
        { "verbose", no_argument, NULL, 'v' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };
    for (;;) {
        int c = getopt_long(argc, argv, "nvh", long_options, NULL);
        if (c == -1)
            break;

        switch (c) {
        case 'n':
            config.print_num_solutions = true;
            config.print_solutions = false;
            break;
        case 'v':
            config.print_attempts = true;
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
        fatal("could not open %s: %s", argv[0], strerror(errno));
    if (read_puzzle(&p, f) < 0) {
        if (ferror(f))
            fatal("error reading %s: %s", argv[0], strerror(errno));
        else
            fatal("incorrect file format");
    }
    solve_puzzle(&p);
    free_puzzle(&p);
    return 0;
}
