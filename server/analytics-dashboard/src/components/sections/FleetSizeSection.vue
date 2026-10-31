<template>
  <DashSection title="Fleet size" :loading="loading" :error="error">
    <template v-if="data">
      <div class="metrics-row">
        <MetricCard
          title="Active Devices"
          :value="data.active_devices"
          :subtitle="`telemetry opt-in only, ${span}`"
        />
        <MetricCard
          title="Fleet (CDN)"
          :value="cdnFleetLatest !== null ? cdnFleetLatest.toLocaleString() : '-'"
          :subtitle="cdnFleetSubtitle"
          color="var(--accent-blue)"
        />
      </div>

      <div class="chart-section">
        <h3>Daily Active Devices</h3>
        <ChartCaption :text="`Distinct opted-in devices with a session per day, ${span}; first and current day omitted`" />
        <NoData v-if="dailyActive.length === 0" />
        <LineChart v-else :data="dailyActiveChartData" />
      </div>

      <div class="chart-section">
        <h3>Cumulative Device Growth</h3>
        <ChartCaption :text="`Opted-in devices first seen in the window, running total, ${span}; first and current day omitted`" />
        <NoData v-if="cumulative.length === 0" />
        <LineChart v-else :data="cumulativeChartData" />
      </div>

      <div class="chart-section">
        <h3>Fleet from Update Polls (CDN)</h3>
        <ChartCaption :text="`Distinct client addresses polling the stable update manifest per UTC day, ${span}; an estimate, not a device count; unfiltered`" />
        <NoData v-if="cdn.length === 0" />
        <LineChart v-else :data="cdnChartData" />
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

const { data, loading, error, span } = useDashData(api.getOverview)

const dailyActive = computed(() => trimPartialDays(data.value?.daily_active_devices ?? [], todayUtc()))
const cumulative = computed(() => trimPartialDays(data.value?.cumulative_devices ?? [], todayUtc()))
const cdn = computed(() => trimPartialDays(data.value?.cdn_fleet ?? [], todayUtc()))

const dailyActiveChartData = computed(() => ({
  labels: dailyActive.value.map(e => e.date),
  datasets: [{
    label: 'Active Devices',
    data: dailyActive.value.map(e => e.devices),
    borderColor: '#8b5cf6',
    backgroundColor: 'rgba(139, 92, 246, 0.1)',
    fill: true,
    tension: 0.3
  }]
}))

const cumulativeChartData = computed(() => ({
  labels: cumulative.value.map(e => e.date),
  datasets: [{
    label: 'Total Devices',
    data: cumulative.value.map(e => e.total),
    borderColor: '#10b981',
    backgroundColor: 'rgba(16, 185, 129, 0.1)',
    fill: true,
    tension: 0.3
  }]
}))

const cdnChartData = computed(() => ({
  labels: cdn.value.map(e => e.date),
  datasets: [{
    label: 'Sources',
    data: cdn.value.map(e => e.sources),
    borderColor: '#3b82f6',
    backgroundColor: 'rgba(59, 130, 246, 0.1)',
    fill: true,
    tension: 0.3
  }]
}))

// Most recent closed day in the CDN series. The cron snapshots the previous
// UTC day, so this lags "now" by up to a day; null until the first run lands.
const cdnFleetLatest = computed<number | null>(() => {
  const series = data.value?.cdn_fleet
  if (!series?.length) return null
  return series[series.length - 1].sources
})

const cdnFleetSubtitle = computed(() => {
  const series = data.value?.cdn_fleet
  if (!series?.length) return 'awaiting first snapshot'
  const latest = series[series.length - 1]
  // Errors here mean installs on the stable channel asked for a manifest and
  // did not get one, which is worth surfacing rather than averaging away.
  if (latest.errors > 0) {
    return `${latest.date} · ${latest.errors.toLocaleString()} failed polls`
  }
  return `${latest.date} · est. from update polls`
})
</script>
