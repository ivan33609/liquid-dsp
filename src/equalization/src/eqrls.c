//
// Recursive least-squares equalizer
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// direct access to element X(i,j) of DxD square matrix
#define square_matrix_direct_access(X,D,i,j) ((X)[(j)*(D)+(i)])

//#define DEBUG

struct EQRLS(_s) {
    unsigned int p;     // filter order
    float lambda;       // RLS forgetting factor
    float delta;        // RLS initialization factor

    T * w0, * w1;       // weights [px1]
    T * P0, * P1;       // recursion matrix [pxp]
    T * g;              // gain vector [px1]

    T * xP0;            // [1xp]
    T zeta;             // constant

    T * gxl;            // [pxp]
    T * gxlP0;          // [pxp]

    unsigned int n;     // counter

    WINDOW() buffer;
};

EQRLS() EQRLS(_create)(unsigned int _p)
{
    EQRLS() eq = (EQRLS()) malloc(sizeof(struct EQRLS(_s)));

    // set filter order, other params
    eq->p = _p;
    eq->lambda = 0.99f;
    eq->delta = 0.1f;
    eq->n=0;

    //
    eq->w0 = (T*) malloc((eq->p)*sizeof(T));
    eq->w1 = (T*) malloc((eq->p)*sizeof(T));
    eq->P0 = (T*) malloc((eq->p)*(eq->p)*sizeof(T));
    eq->P1 = (T*) malloc((eq->p)*(eq->p)*sizeof(T));
    eq->g = (T*) malloc((eq->p)*sizeof(T));

    eq->xP0 = (T*) malloc((eq->p)*sizeof(T));
    eq->gxl = (T*) malloc((eq->p)*(eq->p)*sizeof(T));
    eq->gxlP0 = (T*) malloc((eq->p)*(eq->p)*sizeof(T));

    eq->buffer = WINDOW(_create)(eq->p);

    EQRLS(_reset)(eq);

    return eq;
}

void EQRLS(_destroy)(EQRLS() _eq)
{
    free(_eq->w0);
    free(_eq->w1);
    free(_eq->P0);
    free(_eq->P1);
    free(_eq->g);

    free(_eq->xP0);
    free(_eq->gxl);
    free(_eq->gxlP0);

    WINDOW(_destroy)(_eq->buffer);
    free(_eq);
}

void EQRLS(_print)(EQRLS() _eq)
{
    printf("equalizer (RLS):\n");
    printf("    order:      %u\n", _eq->p);

#ifdef DEBUG
    unsigned int r,c,p=_eq->p;
    printf("P0:\n");
    for (r=0; r<p; r++) {
        for (c=0; c<p; c++) {
            printf("%6.3f ", square_matrix_direct_access(_eq->P0,p,r,c));
        }
        printf("\n");
    }

    printf("P1:\n");
    for (r=0; r<p; r++) {
        for (c=0; c<p; c++) {
            printf("%6.3f ", square_matrix_direct_access(_eq->P1,p,r,c));
        }
        printf("\n");
    }

    printf("gxl:\n");
    for (r=0; r<p; r++) {
        for (c=0; c<p; c++) {
            printf("%6.3f ", square_matrix_direct_access(_eq->gxl,p,r,c));
        }
        printf("\n");
    }
#endif
}

void EQRLS(_reset)(EQRLS() _eq)
{
    unsigned int i, j;
    // initialize...
    for (i=0; i<_eq->p; i++) {
        for (j=0; j<_eq->p; j++) {
            if (i==j)   _eq->P0[(_eq->p)*i + j] = 1.0f / (_eq->delta);
            else        _eq->P0[(_eq->p)*i + j] = 0.0f;
        }
    }

    for (i=0; i<_eq->p; i++)
        _eq->w0[i] = 0.0f;

    WINDOW(_clear)(_eq->buffer);
}

//
//  _x  :   received sample
//  _d  :   desired output
//  _w  :   output weights
void EQRLS(_execute)(EQRLS() _eq, T _x, T _d, T * _w)
{
    unsigned int i,r,c;
    unsigned int p=_eq->p;

    // push value into buffer
    WINDOW(_push)(_eq->buffer, _x);
    T * x;
    _eq->n++;
    if (_eq->n < _eq->p)
        return;
    WINDOW(_read)(_eq->buffer, &x);
#ifdef DEBUG
    printf("x: ");
    for (i=0; i<p; i++)
        printf("%8.4f ", x[i]);
    printf("\n");
#endif

    // compute d_hat (dot product)
    T d_hat;
    DOTPROD(_run)(_eq->w0, x, p, &d_hat);

    // compute error (a priori)
    T alpha = _d - d_hat;
    printf("error: %8.4f\n", alpha);

    // compute gain vector
    for (r=0; r<p; r++) {
        _eq->xP0[r] = 0;
        for (c=0; c<p; c++) {
            _eq->xP0[r] += x[c] * square_matrix_direct_access(_eq->P0,p,r,c);
        }
    }
#ifdef DEBUG
    printf("xP0: ");
    for (c=0; c<p; c++)
        printf("%8.4f ", _eq->xP0[c]);
    printf("\n");
#endif
    // zeta = lambda + [x.']*[P0]*[conj(x)]
    _eq->zeta = 0;
    for (c=0; c<p; c++)
        _eq->zeta += _eq->xP0[c] * conj(x[c]);
    _eq->zeta += _eq->lambda;
#ifdef DEBUG
    printf("zeta : %8.4f\n", _eq->zeta);
#endif
    for (r=0; r<p; r++) {
        _eq->g[r] = 0;
        for (c=0; c<p; c++) {
            _eq->g[r] += square_matrix_direct_access(_eq->P0,p,r,c) * conj(x[c]);
        }
        _eq->g[r] /= _eq->zeta;
    }
#ifdef DEBUG
    printf("g: ");
    for (i=0; i<p; i++)
        printf("%6.3f ", _eq->g[i]);
    printf("\n");
#endif

    // update recursion matrix
    for (r=0; r<p; r++) {
        for (c=0; c<p; c++) {
            // gxl = [g] * [x.'] / lambda
            square_matrix_direct_access(_eq->gxl,p,r,c) = _eq->g[r] * x[c] / _eq->lambda;
        }
    }
    // multiply two [pxp] matrices: gxlP0 = gxl * P0
    for (r=0; r<p; r++) {
        for (c=0; c<p; c++) {
            T sum=0;
            for (i=0; i<p; i++) {
                sum += square_matrix_direct_access(_eq->gxl,  p, r, i) *
                       square_matrix_direct_access(_eq->P0,   p, i, c);
            }
            square_matrix_direct_access(_eq->gxlP0,p,r,c) = sum;
        }
    }
    for (i=0; i<p*p; i++)
        _eq->P1[i] = _eq->P0[i] / _eq->lambda - _eq->gxlP0[i];


    // update weighting vector
    for (i=0; i<p; i++)
        _eq->w1[i] = _eq->w0[i] + alpha*(_eq->g[i]);

    // copy old values
    memmove(_eq->w0, _eq->w1,   p*sizeof(T));
    memmove(_eq->P0, _eq->P1, p*p*sizeof(T));

    // copy output weight vector...
    // TODO: reverse?
    for (i=0; i<p; i++)
        _w[i] = _eq->w1[p-i-1];
    
}

//
//  _w  :   initial weights / output weights
//  _x  :   received sample vector
//  _d  :   desired output vector
//  _n  :   vector length
void EQRLS(_train)(EQRLS() _eq, T * _w, T * _x, T * _d, unsigned int _n)
{
    if (_n < _eq->p) {
        printf("warning: eqrls_xxxt_train(), traning sequence less than filter order\n");
        return;
    }

    unsigned int i;

    // reset equalizer state
    EQRLS(_reset)(_eq);

    // copy initial weights into buffer
    //memmove(_eq->w0, _w, (_eq->p)*sizeof(T));
    for (i=0; i<_eq->p; i++)
        _eq->w0[i] = _w[_eq->p - i - 1];

    for (i=0; i<_n; i++)
        EQRLS(_execute)(_eq, _x[i], _d[i], _w);
}
