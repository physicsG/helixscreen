<template>
  <DashSection title="Adoption curve" :loading="loading" :error="error">
    <div class="chart-section">
      <h3>Devices per version per day</h3>
      <ChartCaption :text="`Distinct devices with a session on each version per day, ${span}; first and current day omitted`" />
      <NoData v-if="dates.length === 0" />
      <LineChart v-else :data="chartData" />
    </div>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import DashSection from '@/components/DashSection.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import LineChart from '@/components/LineChart.vue'
import NoData from '@/components/NoData.vue'
import type { ReleasesData } from '@/services/api'
import { useFiltersStore } from '@/stores/filters'
import { rangeLabel } from '@/utils/format'
import { seriesColor, todayUtc, trimPartialDays } from '@/utils/series'

const props = defineProps<{
  adoption: ReleasesData['adoption']
  /** Version keys in scorecard order; a version's index picks its color. */
  versions: string[]
  loading: boolean
  error: string
}>()

const filters = useFiltersStore()
const span = computed(() => rangeLabel(filters.range))

const dates = computed(() =>
  trimPartialDays(
    [...new Set(props.adoption.map((r) => r.date))].sort().map((date) => ({ date })),
    todayUtc()
  ).map((d) => d.date)
)

const chartData = computed(() => ({
  labels: dates.value,
  datasets: props.versions.map((version, i) => {
    const byDate = new Map(
      props.adoption.filter((r) => r.version === version).map((r) => [r.date, r.devices])
    )
    return {
      label: version,
      data: dates.value.map((d) => byDate.get(d) ?? 0),
      borderColor: seriesColor(i),
      backgroundColor: seriesColor(i),
      fill: false,
      tension: 0.3
    }
  })
}))
</script>
