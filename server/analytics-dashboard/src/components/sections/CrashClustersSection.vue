<template>
  <DashSection title="Crashes" :loading="loading" :error="error">
    <template v-if="data">
      <div class="chart-section">
        <h3>Crash Clusters</h3>
        <ChartCaption :text="`Sample-weighted crash reports grouped by signal, fault code, version and board, ${span}, largest first; click a row to filter on its version and board`" />
        <NoData v-if="data.crash_clusters.length === 0" :not-applicable="data.not_applicable.crashes" />
        <div v-else class="table-wrapper">
          <table class="data-table">
            <thead>
              <tr>
                <th>Signal</th>
                <th>Fault code</th>
                <th>Version</th>
                <th>Board</th>
                <th>Crashes</th>
                <th>Devices</th>
              </tr>
            </thead>
            <tbody>
              <tr
                v-for="(c, i) in data.crash_clusters"
                :key="i"
                class="clickable-row"
                :title="activationHint(clusterEntries(c))"
                @click="filters.activate(router, clusterEntries(c))"
              >
                <td><span class="badge" :class="signalClass(c.signal)">{{ c.signal || '-' }}</span></td>
                <td class="mono">{{ c.fault_code || '-' }}</td>
                <td><span class="badge version">{{ c.version || '-' }}</span></td>
                <td>{{ c.board || '-' }}</td>
                <td class="mono">{{ c.count.toLocaleString() }}</td>
                <td class="mono">{{ c.devices.toLocaleString() }}</td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>

      <div class="grid-2col">
        <div class="chart-section">
          <h3>Crashes by Signal</h3>
          <ChartCaption :text="`Sample-weighted crash reports, ${span}`" />
          <NoData v-if="data.by_signal.length === 0" :not-applicable="data.not_applicable.crashes" />
          <PieChart v-else :data="signalChartData" />
        </div>
        <MetricCard
          title="Avg Uptime Before Crash"
          :value="data.not_applicable.crashes ? 'n/a' : formatDuration(data.avg_uptime_sec)"
          :subtitle="`sample-weighted, ${span}`"
          color="var(--accent-yellow)"
        />
      </div>

      <div class="chart-section">
        <h3>Recent Crashes</h3>
        <ChartCaption :text="`Newest crash reports, ${span}; repeats of one crash collapse into a count`" />
        <NoData v-if="data.recent_crashes.length === 0" :not-applicable="data.not_applicable.crashes" />
        <div v-else class="table-wrapper event-log">
          <table class="data-table">
            <thead>
              <tr>
                <th>Time</th>
                <th>Version</th>
                <th>Signal</th>
                <th>Platform</th>
                <th>Uptime</th>
                <th>Device</th>
                <th>Count</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="(crash, i) in data.recent_crashes" :key="i">
                <td class="mono">{{ formatTimestamp(crash.timestamp) }}</td>
                <td><span class="badge version">{{ crash.version || '-' }}</span></td>
                <td><span class="badge" :class="signalClass(crash.signal)">{{ crash.signal || '-' }}</span></td>
                <td>{{ crash.platform || '-' }}</td>
                <td class="mono">{{ formatDuration(crash.uptime_sec) }}</td>
                <td class="mono device-id">{{ shortDeviceId(crash.device_id) }}</td>
                <td class="mono">
                  <span v-if="crash.occurrences > 1" class="badge occurrences">{{ crash.occurrences }}×</span>
                  <span v-else>1</span>
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRouter } from 'vue-router'
import DashSection from '@/components/DashSection.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import PieChart from '@/components/PieChart.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import type { StabilityData } from '@/services/api'
import { activationHint, single } from '@/utils/drill'
import type { DashFilterKey } from '@/utils/urlState'
import type { Grouped } from '@/utils/versions'
import { formatDuration, formatTimestamp, shortDeviceId } from '@/utils/format'
import { seriesColor } from '@/utils/series'

const { data, loading, error, span, filters } = useDashData(api.getStability)
const router = useRouter()

function clusterEntries(c: StabilityData['crash_clusters'][number]): [DashFilterKey, Grouped][] {
  return [
    ['version', single(c.version)],
    ['board', single(c.board)]
  ]
}

function signalClass(signal: string): string {
  if (signal === 'SIGSEGV') return 'signal-segv'
  if (signal === 'SIGABRT') return 'signal-abrt'
  return 'signal-other'
}

const signalChartData = computed(() => ({
  labels: data.value?.by_signal.map(s => s.signal) ?? [],
  datasets: [{
    data: data.value?.by_signal.map(s => s.count) ?? [],
    backgroundColor: data.value?.by_signal.map((_, i) => seriesColor(i)) ?? []
  }]
}))
</script>

<style scoped>
.badge.signal-segv {
  background: rgba(239, 68, 68, 0.15);
  color: #f87171;
}

.badge.signal-abrt {
  background: rgba(245, 158, 11, 0.15);
  color: #fbbf24;
}

.badge.signal-other {
  background: rgba(139, 92, 246, 0.15);
  color: #a78bfa;
}

.badge.occurrences {
  background: rgba(245, 158, 11, 0.15);
  color: #fbbf24;
}
</style>
