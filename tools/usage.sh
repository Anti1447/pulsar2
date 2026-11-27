#!/usr/bin/env bash
# Read-only snapshot of GPU, CPU and memory use for periodic updates. usage: tools/usage.sh
cd "$(dirname "$0")/.." || exit 1
echo "GPU (whole card, Windows included): $(/usr/lib/wsl/lib/nvidia-smi --query-gpu=memory.used,memory.total,utilization.gpu,power.draw,temperature.gpu --format=csv,noheader)"
log="$(readlink -f run_logs/latest.log)"
for k in 'GPU/Allocated GB' 'GPU/Peak Reserved GB'; do grep -a "^$k" "$log" | tail -n 1; done
echo "trainer peak, max of last 30 iterations: $(grep -a '^GPU/Peak Reserved GB' "$log" | tail -n 30 | awk '{if ($NF > m) m = $NF} END {print m " GB"}')"
echo "WSL CPU, 3 s average ($(nproc) threads): $(top -bn2 -d3 | grep '^%Cpu' | tail -n 1 | awk -F'id,' '{n = split($1, a, ","); gsub(/[^0-9.]/, "", a[n]); printf "%.0f%% busy", 100 - a[n]}'); load$(uptime | awk -F'load average:' '{print $2}')"
for p in /proc/[0-9]*; do
	case "$(readlink "$p/exe" 2>/dev/null)" in "$PWD/build/GigaLearnBot"*)
		echo "trainer process: $(ps -o pcpu=,rss= -p "${p#/proc/}" | awk '{printf "%.0f%% CPU lifetime average, %.1f GB RAM", $1, $2 / 1048576}')" ;;
	esac
done
free -g | awk '/^Mem:/ {print "WSL RAM: " $3 " of " $2 " GB used"}'
win="$(timeout 20 powershell.exe -NoProfile -Command '$o = Get-CimInstance Win32_OperatingSystem; "{0}% CPU, {1:N0} of {2:N0} GB RAM used" -f (Get-CimInstance Win32_Processor | Measure-Object LoadPercentage -Average).Average, (($o.TotalVisibleMemorySize - $o.FreePhysicalMemory) / 1MB), ($o.TotalVisibleMemorySize / 1MB)' 2>/dev/null | tr -d '\r')"
echo "Windows (whole PC): ${win:-not readable}"
