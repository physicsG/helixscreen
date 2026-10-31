<template>
  <DashSection title="Connection stability" :loading="loading" :error="error">
    <template v-if="data">
      <div class="metrics-row">
        <MetricCard
          title="Worst-Case Outage"
          :value="data.max_disconnect_sec === null ? '-' : formatDuration(data.max_disconnect_sec)"
          :subtitle="data.max_disconnect_sec === null ? deltaEraPlaceholder(data) : `longest single outage, ${span}`"
          color="var(--accent-red)"
        />
      </div>

      <div class="chart-section">
        <h3>Uptime Over Time</h3>
        <ChartCaption :text="`Connected time over observed time per day, sample-weighted, ${span}; first and current day omitted`" />
        <DeltaEraWaiting v-if="data.delta_era_devices === 0 || data.filter_not_applicable" :era="data" />
        <NoData v-else-if="uptime.length === 0" :era="data" />
        <LineChart v-else :data="uptimeChartData" :options="uptimeOpts" />
      </div>

      <div class="chart-section">
        <h3>Disconnects per Session Over Time</h3>
        <ChartCaption :text="`Average Moonraker disconnects per boot per day, ${span}; first and current day omitted`" />
        <DeltaEraWaiting v-if="data.delta_era_devices === 0 || data.filter_not_applicable" :era="data" />
        <NoData v-else-if="disconnects.length === 0" :era="data" />
        <LineChart v-else :data="disconnectChartData" />
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { ChartOptions } from 'chart.js'
import DashSection from '@/components/DashSection.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import DeltaEraWaiting from '@/components/DeltaEraWaiting.vue'
import LineChart from '@/components/LineChart.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { rateAxis } from '@/utils/chart'
import { deltaEraPlaceholder, formatDuration, formatRate } from '@/utils/format'
import { todayUtc, trimPartialDays } from '@/utils/series'

const { data, loading, error, span } = useDashData(api.getReliability)

const uptimeOpts: ChartOptions<'line'> = {
  scales: { y: { ...rateAxis(), max: 1 } },
  plugins: { tooltip: { callbacks: { label: (ctx) => `Uptime: ${formatRate(Number(ctx.raw), 2)}` } } }
}

const uptime = computed(() => trimPartialDays(data.value?.uptime_trend ?? [], todayUtc()))
const disconnects = computed(() => trimPartialDays(data.value?.disconnect_trend ?? [], todayUtc()))

const uptimeChartData = computed(() => ({
  labels: uptime.value.map(d => d.date),
  datasets: [{
    label: 'Uptime',
    data: uptime.value.map(d => d.avg_uptime_pct / 100),
    borderColor: '#10b981',
    backgroundColor: 'rgba(16, 185, 129, 0.1)',
    fill: true,
    tension: 0.3
  }]
}))

const disconnectChartData = computed(() => ({
  labels: disconnects.value.map(d => d.date),
  datasets: [{
    label: 'Avg Disconnects',
    data: disconnects.value.map(d => d.avg_disconnects),
    borderColor: '#ef4444',
    backgroundColor: 'rgba(239, 68, 68, 0.1)',
    fill: true,
    tension: 0.3
  }]
}))
</script>
