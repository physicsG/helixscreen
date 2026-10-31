<template>
  <DashSection title="Success by material set" :loading="loading" :error="error">
    <template v-if="data">
      <div class="metrics-row">
        <MetricCard
          title="Multi-material Share"
          :value="formatRate(multiShare)"
          :subtitle="`prints with two or more materials, ${span}`"
          color="var(--accent-purple)"
        />
      </div>

      <div class="chart-section">
        <h3>Success by Material Set</h3>
        <ChartCaption
          :text="`Sample-weighted print outcomes per set of distinct materials loaded, ${span}; sets with fewer than ${LOW_COUNT_PRINTS} prints are dimmed`"
        />
        <NoData v-if="rows.length === 0" :not-applicable="data.not_applicable.prints" />
        <BarChart
          v-else
          :data="chartData"
          :options="options"
          :height="barChartHeight(rows.length)"
        />
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
import type { ChartOptions } from 'chart.js'
import { TICK_COLOR, mergeOptions, successBarOpts, type SuccessRow } from '@/utils/chart'
import { formatRate } from '@/utils/format'
import { LOW_COUNT_PRINTS, groupMaterialSets, isLowCount } from '@/utils/materials'
import { barChartHeight, dimmed, seriesColor } from '@/utils/series'

const { data, loading, error, span } = useDashData(api.getPrints)

const sets = computed(() => groupMaterialSets(data.value?.by_filament ?? []))

const rows = computed((): SuccessRow[] => sets.value.map((s) => ({ ...s, rate: s.success_rate })))

const multiShare = computed((): number | null => {
  const total = sets.value.reduce((sum, s) => sum + s.count, 0)
  if (total === 0) return null
  return sets.value.filter((s) => s.multi).reduce((sum, s) => sum + s.count, 0) / total
})

const chartData = computed(() => ({
  labels: rows.value.map((r) => `${r.label} (${r.count.toLocaleString()})`),
  datasets: [{
    label: 'Success rate',
    data: rows.value.map((r) => r.rate),
    backgroundColor: rows.value.map((r, i) => (isLowCount(r.count) ? dimmed(seriesColor(i)) : seriesColor(i)))
  }]
}))

// Every set is listed; one with few prints is drawn dimmed, bar and label.
const options = computed(
  (): ChartOptions<'bar'> =>
    mergeOptions(successBarOpts(rows.value, false), {
      scales: {
        y: {
          ticks: {
            color: (ctx: { index: number }) => {
              const row = rows.value[ctx.index]
              return row && isLowCount(row.count) ? dimmed(TICK_COLOR) : TICK_COLOR
            }
          }
        }
      }
    })
)
</script>
