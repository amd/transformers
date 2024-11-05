import argparse
import csv
import os
import statistics
import sys

import pandas as pd


def main():

    # # Create a parser
    parser = argparse.ArgumentParser(description="rmsnorm benchmark")

    parser.add_argument(
        "--file", type=str, required=True, help="CSV file generated by rmsnorm"
    )

    args = parser.parse_args()

    env_var_name = "NUM_EXECUTE_ITERATIONS"

    if env_var_name in os.environ:
        num_iter = os.environ["NUM_EXECUTE_ITERATIONS"]
    else:
        num_iter = 1

    if not os.path.exists(args.file):
        raise FileNotFoundError("- Error: No such file {}".format(args.file))

    exe_median = []
    aie_run_median = []
    a_copy_median = []
    a_sync_median = []
    b_copy_median = []
    b_sync_median = []
    c_copy_median = []
    c_sync_median = []

    with open(args.file) as file:
        reader = csv.reader(file, delimiter=" ")

        exe_per_shape = []
        aie_run_per_shape = []
        a_copy_per_shape = []
        a_sync_per_shape = []
        b_copy_per_shape = []
        b_sync_per_shape = []
        c_copy_per_shape = []
        c_sync_per_shape = []
        first_row = True
        M = []
        K = []

        for row in reader:
            row = list(row[:])

            if first_row:
                fields = [
                    row[1],
                    row[2],
                    row[3],
                    row[5],
                    row[6],
                    row[7],
                    row[8],
                    row[9],
                    row[10],
                    row[11],
                ]
                first_row = False
            else:
                row = list(map(float, row[:]))
                exe_per_shape.append(float(row[3]))
                aie_run_per_shape.append(float(row[5]))
                a_copy_per_shape.append(float(row[6]))
                a_sync_per_shape.append(float(row[7]))
                b_copy_per_shape.append(float(row[8]))
                b_sync_per_shape.append(float(row[9]))
                c_copy_per_shape.append(float(row[10]))
                c_sync_per_shape.append(float(row[11]))

                if len(exe_per_shape) == int(num_iter):

                    exe_median.append(statistics.median(exe_per_shape))
                    aie_run_median.append(statistics.median(aie_run_per_shape))
                    a_copy_median.append(statistics.median(a_copy_per_shape))
                    a_sync_median.append(statistics.median(a_sync_per_shape))
                    b_copy_median.append(statistics.median(b_copy_per_shape))
                    b_sync_median.append(statistics.median(b_sync_per_shape))
                    c_copy_median.append(statistics.median(c_copy_per_shape))
                    c_sync_median.append(statistics.median(c_sync_per_shape))
                    M.append(row[1])
                    K.append(row[2])

                    exe_per_shape = []
                    aie_run_per_shape = []
                    a_copy_per_shape = []
                    a_sync_per_shape = []
                    b_copy_per_shape = []
                    b_sync_per_shape = []
                    c_copy_per_shape = []
                    c_sync_per_shape = []

    data_lists = [
        M,
        K,
        exe_median,
        aie_run_median,
        a_copy_median,
        a_sync_median,
        b_copy_median,
        b_sync_median,
        c_copy_median,
        c_sync_median,
    ]

    # name of csv file
    filename = "rmsnorm_bm.csv"

    # # writing to csv file
    with open(filename, mode="w", newline="") as csvfile:
        # creating a csv dict writer object
        csvwriter = csv.writer(csvfile)
        csvwriter.writerow(fields)
        for i in range(len(data_lists[0])):
            row_values = [data[i] for data in data_lists]
            csvwriter.writerow(row_values)


if __name__ == "__main__":
    main()
