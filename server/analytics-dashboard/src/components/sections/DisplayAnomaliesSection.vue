<template>
  <DashSection title="LVGL display anomalies" :loading="loading" :error="error">
    <template v-if="data">
      <p class="section-subtitle">Double-schedule, use-after-free near-miss, event chain corruption</p>
      <NoData v-if="!anomalies" />
      <template v-else>
        <div class="metrics-row">
          <MetricCard
            title="Display Anomalies"
            :value="totalDisplayAnomalies.toLocaleString()"
            :subtitle="span"
            color="#a855f7"
          />
          <MetricCard
            title="Affected Devices"
            :value="(anomalies.affected_devices ?? 0).toLocaleString()"
            subtitle="with anomalies"
            color="#8b5cf6"
          />
          <MetricCard
            title="Anomaly Codes"
            :value="(anomalies.by_code?.length ?? 0).toString()"
            subtitle="distinct types"
            color="#7c3aed"
          />
          <MetricCard
            title="Affected Versions"
            :value="(anomalies.by_version?.length ?? 0).toString()"
            subtitle="with anomalies"
            color="#6d28d9"
          />
        </div>

        <div class="chart-section" v-if="trend.length > 0">
          <h3>Display Anomalies Over Time</h3>
          <ChartCaption :text="`Sample-weighted display anomaly reports per day, ${span}; first and current day omitted`" />
          <LineChart :data="displayTrendChartData" />
        </div>

        <div class="grid-2col" v-if="anomalies.by_code.length > 0 || anomalies.by_version.length > 0">
          <div class="chart-section" v-if="anomalies.by_code.length > 0">
            <h3>By Anomaly Code</h3>
            <ChartCaption :text="`Sample-weighted display anomaly reports, ${span}`" />
            <BarChart
              :data="displayCodeChartData"
              :options="horizontalBarOpts"
              :height="barChartHeight(anomalies.by_code.length)"
            />
          </div>
          <DistributionBar
            v-if="anomalies.by_version.length > 0"
            :rows="versionRows"
            filter-key="version"
            title="By Version"
            value-label="Anomalies"
            :caption="`Sample-weighted display anomaly reports, ${span}`"
          />
        </div>

        <div class="chart-section">
          <h3>Recent Display Anomalies</h3>
          <ChartCaption :text="`Newest display anomaly reports, ${span}`" />
          <NoData v-if="!anomalies.recent?.length" />
          <div v-else class="table-wrapper event-log">
            <table class="data-table">
              <thead>
                <tr>
                  <th>Time</th>
                  <th>Version</th>
                  <th>Platform</th>
                  <th>Code</th>
                  <th>Uptime</th>
                  <th>Device</th>
                  <th>Context</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="(row, i) in anomalies.recent" :key="i">
                  <td class="mono">{{ formatTimestamp(row.timestamp) }}</td>
                  <td><span class="badge version">{{ row.version || '-' }}</span></td>
                  <td>{{ row.platform || '-' }}</td>
                  <td><span class="badge anomaly-code">{{ row.code }}</span></td>
                  <td class="mono">{{ formatDuration(row.uptime_sec) }}</td>
                  <td class="mono device-id">{{ shortDeviceId(row.device_id) }}</td>
                  <td class="mono context-cell" :title="row.context">{{ truncateContext(row.context) }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </template>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import DashSection from '@/components/DashSection.vue'
import BarChart from '@/components/BarChart.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import DistributionBar from '@/components/DistributionBar.vue'
import LineChart from '@/components/LineChart.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { horizontalBarOpts } from '@/utils/chart'
import { formatDuration, formatTimestamp, shortDeviceId } from '@/utils/format'
import { barChartHeight, todayUtc, trimPartialDays } from '@/utils/series'
import { groupVersions } from '@/utils/versions'

const { data, loading, error, span, filters } = useDashData(api.getStability)

const anomalies = computed(() => data.value?.display_anomalies ?? null)

const totalDisplayAnomalies = computed(() =>
  (anomalies.value?.trend ?? []).reduce((sum, d) => sum + d.count, 0)
)

const trend = computed(() => trimPartialDays(anomalies.value?.trend ?? [], todayUtc()))

const displayTrendChartData = computed(() => ({
  labels: trend.value.map(d => d.date),
  datasets: [{
    label: 'Anomalies',
    data: trend.value.map(d => d.count),
    borderColor: '#a855f7',
    backgroundColor: 'rgba(168, 85, 247, 0.1)',
    fill: true,
    tension: 0.3
  }]
}))

const displayCodeChartData = computed(() => ({
  labels: anomalies.value?.by_code.map(c => c.code) ?? [],
  datasets: [{
    label: 'Count',
    data: anomalies.value?.by_code.map(c => c.count) ?? [],
    backgroundColor: '#a855f7'
  }]
}))

const versionRows = computed(() =>
  groupVersions(
    (anomalies.value?.by_version ?? []).map(v => ({ name: v.version, count: v.count })),
    new Set(filters.expand)
  )
)

// The descriptive part before the backtrace; the cell's CSS ellipsis trims the rest.
function truncateContext(ctx: string): string {
  if (!ctx) return '-'
  const btIdx = ctx.indexOf(' | bt=')
  return btIdx >= 0 ? ctx.slice(0, btIdx) : ctx
}
</script>

<style scoped>
.section-subtitle {
  font-size: 13px;
  color: var(--text-secondary);
  margin: -8px 0 16px;
}

.badge.anomaly-code {
  background: rgba(168, 85, 247, 0.15);
  color: #c084fc;
}

.context-cell {
  max-width: 300px;
  overflow: hidden;
  text-overflow: ellipsis;
  font-size: 11px;
  color: var(--text-secondary);
}
</style>
