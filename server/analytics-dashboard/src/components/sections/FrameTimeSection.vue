<template>
  <DashSection title="Frame time" :loading="loading" :error="error">
    <template v-if="data">
      <div class="metrics-row">
        <MetricCard
          title="Median Frame Time"
          :value="`${data.fleet_p50_ms}ms`"
          :subtitle="`sample-weighted, ${span}`"
          color="var(--accent-blue)"
        />
      </div>

      <div class="chart-section">
        <h3>Frame Time Trends (ms)</h3>
        <ChartCaption :text="`Sample-weighted average of each device's p50, p95 and p99 frame time per day, ${span}; first and current day omitted`" />
        <NoData v-if="trend.length === 0" />
        <LineChart v-else :data="frameTimeTrendData" />
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import DashSection from '@/components/DashSection.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import LineChart from '@/components/LineChart.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { todayUtc, trimPartialDays } from '@/utils/series'

const { data, loading, error, span } = useDashData(api.getPerformance)

const trend = computed(() => trimPartialDays(data.value?.frame_time_trend ?? [], todayUtc()))

const frameTimeTrendData = computed(() => ({
  labels: trend.value.map(d => d.date),
  datasets: [
    {
      label: 'p50',
      data: trend.value.map(d => d.p50),
      borderColor: '#10b981',
      fill: false,
      tension: 0.3,
    },
    {
      label: 'p95',
      data: trend.value.map(d => d.p95),
      borderColor: '#f59e0b',
      fill: false,
      tension: 0.3,
    },
    {
      label: 'p99',
      data: trend.value.map(d => d.p99),
      borderColor: '#ef4444',
      fill: false,
      tension: 0.3,
    },
  ],
}))
</script>
