<template>
  <DashSection title="Panels" :loading="loading" :error="error">
    <template v-if="data && ux">
      <div class="metrics-row">
        <MetricCard
          title="Avg Session"
          :value="data.avg_session_sec === null ? '-' : formatDuration(data.avg_session_sec)"
          :subtitle="data.avg_session_sec === null ? deltaEraPlaceholder(data) : `per boot, ${span}`"
          color="var(--accent-blue)"
        />
        <MetricCard
          title="Most Visited Panel"
          :value="ux.most_visited_panel || 'N/A'"
          color="var(--accent-green)"
        />
        <MetricCard
          title="Least Visited Panel"
          :value="ux.least_visited_panel || 'N/A'"
          color="var(--accent-red)"
        />
      </div>

      <div class="chart-section">
        <h3>Time per Panel</h3>
        <ChartCaption :text="`Sample-weighted seconds on each panel, all devices, ${span}`" />
        <DeltaEraWaiting v-if="waiting" :era="data" />
        <NoData v-else-if="panelTime.length === 0" :era="data" />
        <BarChart v-else :data="panelTimeChartData" :options="horizontalBarOpts" :height="barChartHeight(panelTime.length)" />
      </div>

      <div class="grid-2col">
        <div class="chart-section">
          <h3>Visit Count per Panel</h3>
          <ChartCaption :text="`Sample-weighted panel visits, all devices, ${span}`" />
          <DeltaEraWaiting v-if="waiting" :era="data" />
          <NoData v-else-if="data.panel_visits.length === 0" :era="data" />
          <BarChart v-else :data="panelVisitsChartData" :options="horizontalBarOpts" :height="barChartHeight(data.panel_visits.length)" />
        </div>
        <div class="chart-section">
          <h3>Visits per Panel (per device)</h3>
          <ChartCaption :text="`Panel visits divided by reporting devices, ${span}`" />
          <DeltaEraWaiting v-if="uxWaiting" :era="ux" />
          <NoData v-else-if="perDevice.length === 0" :era="ux" />
          <BarChart v-else :data="panelVisitsBarData" :options="horizontalBarOpts" :height="barChartHeight(perDevice.length)" />
        </div>
      </div>

      <div class="chart-section" v-if="waiting || data.overlay_visits?.length">
        <h3>Visit Count per Overlay</h3>
        <ChartCaption :text="`Sample-weighted overlay opens, all devices, ${span}`" />
        <DeltaEraWaiting v-if="waiting" :era="data" />
        <BarChart v-else :data="overlayVisitsChartData" :options="horizontalBarOpts" :height="barChartHeight(data.overlay_visits.length)" />
      </div>

      <div class="chart-section">
        <h3>Avg Session Duration Over Time</h3>
        <ChartCaption :text="`Sample-weighted average boot length per day, ${span}; first and current day omitted`" />
        <DeltaEraWaiting v-if="waiting" :era="data" />
        <NoData v-else-if="sessionTrend.length === 0" :era="data" />
        <LineChart v-else :data="sessionDurationChartData" />
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import DashSection from '@/components/DashSection.vue'
import BarChart from '@/components/BarChart.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import DeltaEraWaiting from '@/components/DeltaEraWaiting.vue'
import LineChart from '@/components/LineChart.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import type { DeltaEra } from '@/services/api'
import { horizontalBarOpts } from '@/utils/chart'
import { deltaEraPlaceholder, formatDuration } from '@/utils/format'
import { barChartHeight, todayUtc, trimPartialDays } from '@/utils/series'

const engagement = useDashData(api.getEngagement)
const uxData = useDashData(api.getUxInsights)
const data = engagement.data
const ux = uxData.data
const span = engagement.span
const loading = computed(() => engagement.loading.value || uxData.loading.value)
const error = computed(() => engagement.error.value || uxData.error.value)

const isWaiting = (era: DeltaEra | null) => !!era && (era.delta_era_devices === 0 || era.filter_not_applicable !== null)
const waiting = computed(() => isWaiting(data.value))
const uxWaiting = computed(() => isWaiting(ux.value))

const panelTime = computed(() =>
  [...(data.value?.panel_time ?? [])].sort((a, b) => b.total_time_sec - a.total_time_sec)
)

const panelTimeChartData = computed(() => ({
  labels: panelTime.value.map(p => `${p.panel} (${formatDuration(p.total_time_sec)})`),
  datasets: [{
    label: 'Seconds',
    data: panelTime.value.map(p => p.total_time_sec),
    backgroundColor: '#3b82f6'
  }]
}))

const panelVisitsChartData = computed(() => ({
  labels: data.value?.panel_visits.map(p => p.panel) ?? [],
  datasets: [{
    label: 'Visits',
    data: data.value?.panel_visits.map(p => p.total_visits) ?? [],
    backgroundColor: '#10b981'
  }]
}))

const perDevice = computed(() =>
  [...(ux.value?.panel_visits ?? [])].sort((a, b) => b.visits_per_device - a.visits_per_device)
)

const panelVisitsBarData = computed(() => ({
  labels: perDevice.value.map(p => p.panel),
  datasets: [{
    label: 'Visits/Device',
    data: perDevice.value.map(p => +p.visits_per_device.toFixed(1)),
    backgroundColor: '#10b981',
  }],
}))

const overlayVisitsChartData = computed(() => ({
  labels: data.value?.overlay_visits?.map(o => o.overlay) ?? [],
  datasets: [{
    label: 'Visits',
    data: data.value?.overlay_visits?.map(o => o.total_visits) ?? [],
    backgroundColor: '#f59e0b'
  }]
}))

const sessionTrend = computed(() => trimPartialDays(data.value?.session_duration_trend ?? [], todayUtc()))

const sessionDurationChartData = computed(() => ({
  labels: sessionTrend.value.map(d => d.date),
  datasets: [{
    label: 'Avg Duration (s)',
    data: sessionTrend.value.map(d => d.avg_session_sec),
    borderColor: '#8b5cf6',
    backgroundColor: 'rgba(139, 92, 246, 0.1)',
    fill: true,
    tension: 0.3
  }]
}))
</script>
