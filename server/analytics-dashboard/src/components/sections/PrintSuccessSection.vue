<template>
  <DashSection title="Print success" :loading="loading" :error="error">
    <template v-if="data">
      <div class="metrics-row">
        <MetricCard
          title="Average Print Duration"
          :value="formatDuration(data.avg_duration_sec)"
          :subtitle="`sample-weighted, all print outcomes, ${span}`"
        />
      </div>

      <div class="chart-section">
        <h3>Success Rate Over Time</h3>
        <ChartCaption :text="`Sample-weighted print outcomes per day, ${span}; first and current day omitted`" />
        <NoData v-if="trend.length === 0" :not-applicable="data.not_applicable.prints" />
        <LineChart v-else :data="successRateChartData" :options="rateOpts" />
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { ChartOptions } from 'chart.js'
import DashSection from '@/components/DashSection.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import LineChart from '@/components/LineChart.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { rateAxis } from '@/utils/chart'
import { formatDuration, formatRate } from '@/utils/format'
import { todayUtc, trimPartialDays } from '@/utils/series'

const { data, loading, error, span } = useDashData(api.getPrints)

const trend = computed(() => trimPartialDays(data.value?.success_rate_over_time ?? [], todayUtc()))

const rateOpts = computed((): ChartOptions<'line'> => ({
  scales: { y: { ...rateAxis(), max: 1 } },
  plugins: {
    tooltip: {
      callbacks: {
        label: (ctx) => {
          const point = trend.value[ctx.dataIndex]
          return point ? `${formatRate(point.rate)} of ${point.total} prints` : formatRate(Number(ctx.raw))
        }
      }
    }
  }
}))

const successRateChartData = computed(() => ({
  labels: trend.value.map(d => d.date),
  datasets: [{
    label: 'Success Rate',
    data: trend.value.map(d => d.rate),
    borderColor: '#10b981',
    backgroundColor: 'rgba(16, 185, 129, 0.1)',
    fill: true,
    tension: 0.3
  }]
}))
</script>
