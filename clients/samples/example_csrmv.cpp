/* ************************************************************************
 * Copyright 2018 Advanced Micro Devices, Inc.
 * ************************************************************************ */

#include "utility.hpp"

#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <rocsparse.h>

int main(int argc, char *argv[])
{
    // Parse command line
    if (argc < 2)
    {
        fprintf(stderr, "%s <ndim> [<trials> <batch_size>]\n", argv[0]);
        return -1;
    }

    int ndim = atoi(argv[1]);
    int trials = 200;
    int batch_size = 1;

    if (argc > 2)
    {
        trials = atoi(argv[2]);
    }
    if (argc > 3)
    {
        batch_size = atoi(argv[3]);
    }

    // rocSPARSE handle
    rocsparse_handle handle;
    rocsparse_create_handle(&handle);

    hipDeviceProp_t devProp;
    int device_id = 0;

    hipGetDevice(&device_id);
    hipGetDeviceProperties(&devProp, device_id);
    printf("Device: %s\n", devProp.name);

    // Generate problem
    std::vector<int> hAptr;
    std::vector<int> hAcol;
    std::vector<double> hAval;
    int nrow = gen_2d_laplacian(ndim, hAptr, hAcol, hAval);
    int ncol = nrow;
    int nnz = hAptr[nrow];

    // Sample some random data
    srand(12345ULL);

    double halpha = (double) rand() / RAND_MAX;
    double hbeta  = 0.0;

    std::vector<double> hx(nrow);
    rocsparse_init(hx, 1, nrow);

    // Matrix descriptor
    rocsparse_mat_descr descrA;
    rocsparse_create_mat_descr(&descrA);

    // Offload data to device
    int *dAptr = NULL;
    int *dAcol = NULL;
    double *dAval = NULL;
    double *dx = NULL;
    double *dy = NULL;

    hipMalloc((void**) &dAptr, sizeof(int)*(nrow+1));
    hipMalloc((void**) &dAcol, sizeof(int)*nnz);
    hipMalloc((void**) &dAval, sizeof(double)*nnz);
    hipMalloc((void**) &dx, sizeof(double)*nrow);
    hipMalloc((void**) &dy, sizeof(double)*nrow);

    hipMemcpy(dAptr, hAptr.data(), sizeof(int)*(nrow+1), hipMemcpyHostToDevice);
    hipMemcpy(dAcol, hAcol.data(), sizeof(int)*nnz, hipMemcpyHostToDevice);
    hipMemcpy(dAval, hAval.data(), sizeof(double)*nnz, hipMemcpyHostToDevice);
    hipMemcpy(dx, hx.data(), sizeof(double)*nrow, hipMemcpyHostToDevice);

    // Warm up
    for (int i=0; i<10; ++i)
    {
        // Call rocsparse csrmv
        rocsparse_dcsrmv(handle, rocsparse_operation_none,
                         nrow, ncol, nnz,
                         &halpha, descrA,
                         dAval, dAptr, dAcol,
                         dx, &hbeta, dy);
    }

    // Device synchronization
    hipDeviceSynchronize();

    // Start time measurement
    double time = get_time_us();

    // CSR matrix vector multiplication
    for (int i=0; i<trials; ++i)
    {
        for (int i=0; i<batch_size; ++i)
        {
            // Call rocsparse csrmv
            rocsparse_dcsrmv(handle, rocsparse_operation_none,
                             nrow, ncol, nnz,
                             &halpha, descrA,
                             dAval, dAptr, dAcol,
                             dx, &hbeta, dy);
        }

        // Device synchronization
        hipDeviceSynchronize();
    }

    time = (get_time_us() - time) / (trials * batch_size * 1e3);
    double bandwidth = static_cast<double>(sizeof(double)*(2*nrow+nnz)
                                          +sizeof(rocsparse_int)*(nrow+1+nnz))/time/1e6;
    double gflops = static_cast<double>(2*nnz)/time/1e6;
    printf("nrow\t\tncol\t\tnnz\t\talpha\tbeta\tGFlops\tGB/s\tusec\n");
    printf("%8d\t%8d\t%9d\t%0.2lf\t%0.2lf\t%0.2lf\t%0.2lf\t%0.2lf\n",
           nrow, ncol, nnz, halpha, hbeta, gflops, bandwidth, time);




    // Clear up on device
    hipFree(dAptr);
    hipFree(dAcol);
    hipFree(dAval);
    hipFree(dx);
    hipFree(dy);

    rocsparse_destroy_mat_descr(descrA);
    rocsparse_destroy_handle(handle);

    return 0;
}
