sudo sh -c "echo 0 > /proc/sys/kernel/randomize_va_space"
sudo sh -c "echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo"
sudo sh -c "echo off > /sys/devices/system/cpu/smt/control"

for i in /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
do
    echo performance > ${i}
done

make all
sudo rmmod fibdrv_new.ko
sudo insmod fibdrv_new.ko
sudo taskset 1 ./measure > measure_data
sudo rmmod fibdrv_new.ko
gnuplot plot.gp



