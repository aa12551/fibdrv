set title "Performance"
set xlabel "n th fibonacci number"
set ylabel " time (ns) "
set terminal png font " Times_New_Roman,12 "
set output "statistic.png"
set xtics 0 ,500 ,5000
set key left 

plot \
"measure_data" using 1:2 with linespoints linewidth 2 title "recursive", \
"measure_data" using 1:3 with linespoints linewidth 2 title "fast doubling v0", \

