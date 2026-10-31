<template>
  <DashSection title="Jankiest panels" :loading="loading" :error="error">
    <template v-if="data">
      <div class="metrics-row">
        <MetricCard
          title="Worst Panel"
          :value="data.worst_panel || 'N/A'"
          :subtitle="`most often the slowest panel, ${span}`"
          color="var(--accent-purple)"
        />
      </div>

      <div class="chart-section">
        <h3>Fleet-wide (by p95 frame time)</h3>
        <ChartCaption :text="`Panels reported as a device's slowest, sample-weighted average of that panel's p95 frame time, ${span}`" />
        <NoData v-if="fleet.length === 0" />
        <BarChart
          v-else
          :data="jankiestPanelsData"
          :options="horizontalBarOpts"
          :height="barChartHeight(fleet.length)"
        />
      </div>

      <div class="chart-section">
        <h3>Per Platform</h3>
        <ChartCaption :text="`Same measure split by platform, slowest first; times worst counts sample-weighted reports, ${span}`" />
        <NoData v-if="byPlatform.length === 0" />
        <div v-else class="platform-grid">
          <div v-for="group in byPlatform" :key="group.platform" class="table-wrapper">
            <table class="data-table">
              <thead>
                <tr>
                  <th>{{ group.platform }}</th>
                  <th>p95</th>
                  <th>Times worst</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="p in group.panels" :key="p.panel">
                  <td>{{ p.panel }}</td>
                  <td class="mono">{{ p.avg_p95_ms.toFixed(1) }} ms</td>
                  <td class="mono">{{ p.times_worst.toLocaleString() }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import DashSection from '@/components/DashSection.vue'
import BarChart from '@/components/BarChart.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import type { PerformanceData } from '@/services/api'
import { horizontalBarOpts } from '@/utils/chart'
import { barChartHeight } from '@/utils/series'

const { data, loading, error, span } = useDashData(api.getPerformance)

const fleet = computed(() =>
  [...(data.value?.jankiest_panels ?? [])].sort((a, b) => b.avg_p95_ms - a.avg_p95_ms)
)

const jankiestPanelsData = computed(() => ({
  labels: fleet.value.map(p => `${p.panel} (${p.avg_p95_ms}ms)`),
  datasets: [{
    label: 'Avg p95 (ms)',
    data: fleet.value.map(p => p.avg_p95_ms),
    backgroundColor: '#8b5cf6',
  }],
}))

type PlatformPanel = PerformanceData['jankiest_by_platform'][number]

const byPlatform = computed(() => {
  const groups = new Map<string, PlatformPanel[]>()
  for (const row of data.value?.jankiest_by_platform ?? []) {
    const list = groups.get(row.platform)
    if (list) list.push(row)
    else groups.set(row.platform, [row])
  }
  return [...groups.entries()]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([platform, panels]) => ({
      platform,
      panels: panels.sort((a, b) => b.avg_p95_ms - a.avg_p95_ms)
    }))
})
</script>

<style scoped>
.platform-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 12px;
}
</style>
