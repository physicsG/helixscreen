<template>
  <DashSection title="Memory warnings" :loading="loading" :error="error">
    <template v-if="data">
      <p class="section-subtitle">Threshold breaches detected by MemoryMonitor</p>

      <NoData v-if="data.total_warnings === 0" />
      <template v-else>
        <div class="metrics-row">
          <MetricCard
            title="Total Warnings"
            :value="String(data.total_warnings)"
            :subtitle="`threshold breaches, ${span}`"
            color="var(--accent-yellow)"
          />
          <MetricCard
            title="Affected Devices"
            :value="String(data.affected_devices)"
            subtitle="unique devices"
            color="var(--accent-purple)"
          />
          <MetricCard
            title="Critical"
            :value="String(criticalCount)"
            subtitle="critical level"
            color="var(--accent-red)"
          />
          <MetricCard
            title="Avg RSS at Warning"
            :value="formatMB(avgRssAtWarning)"
            subtitle="when threshold hit"
            color="var(--accent-blue)"
          />
        </div>

        <div class="chart-section">
          <h3>Warnings Over Time</h3>
          <ChartCaption :text="`Sample-weighted memory warnings per day by level, ${span}; first and current day omitted`" />
          <NoData v-if="warningsOverTimeChartData.labels.length === 0" />
          <BarChart v-else :data="warningsOverTimeChartData" :options="stackedBarOpts" />
        </div>

        <div class="chart-section">
          <h3>RSS at Warning Time</h3>
          <ChartCaption :text="`Resident memory when a warning fired, sample-weighted average and maximum per day, ${span}; first and current day omitted`" />
          <NoData v-if="rssAtWarning.length === 0" />
          <LineChart v-else :data="rssAtWarningChartData" />
        </div>

        <DistributionBar
          :rows="ungrouped(data.by_platform.map((p) => ({ name: p.platform, count: p.count })))"
          filter-key="platform"
          title="Warnings by Platform"
          value-label="Warnings"
          :caption="`Sample-weighted memory warnings, ${span}`"
        />

        <div class="chart-section">
          <h3>Recent Warnings</h3>
          <ChartCaption :text="`Newest memory warnings, ${span}; click a row for its breakdown`" />
          <div class="table-container event-log">
            <table class="warnings-table">
              <thead>
                <tr>
                  <th>Time</th>
                  <th>Level</th>
                  <th>Device</th>
                  <th>Version</th>
                  <th>Platform</th>
                  <th>RSS</th>
                  <th>Avail</th>
                  <th>Growth/5m</th>
                  <th>Reason</th>
                </tr>
              </thead>
              <tbody>
                <template v-for="(w, idx) in data.recent_warnings" :key="w.timestamp + w.device_id">
                  <tr class="warning-row" :class="{ expanded: expandedRow === idx }" @click="toggleRow(idx)">
                    <td class="mono">{{ formatTimestamp(w.timestamp) }}</td>
                    <td>
                      <span class="level-badge" :class="'level-' + w.level">{{ w.level }}</span>
                    </td>
                    <td class="mono device-id">{{ shortDeviceId(w.device_id) }}</td>
                    <td>{{ w.version }}</td>
                    <td>{{ w.platform }}</td>
                    <td class="mono">{{ formatMB(w.rss_kb) }}</td>
                    <td class="mono">{{ w.system_available_mb }} MB</td>
                    <td class="mono">{{ formatGrowth(w.growth_5min_kb) }}</td>
                    <td class="reason-cell">
                      <span class="expand-icon">{{ expandedRow === idx ? '▼' : '▶' }}</span>
                      {{ w.reason }}
                    </td>
                  </tr>
                  <tr v-if="expandedRow === idx" class="detail-row">
                    <td colspan="9">
                      <div class="detail-grid">
                        <div class="detail-section">
                          <h4>Memory Breakdown</h4>
                          <div class="detail-items">
                            <div class="detail-item">
                              <span class="detail-label">RSS</span>
                              <span class="detail-value mono">{{ formatMB(w.rss_kb) }}</span>
                            </div>
                            <div class="detail-item">
                              <span class="detail-label">PSS</span>
                              <span class="detail-value mono">{{ formatMB(w.pss_kb) }}</span>
                            </div>
                            <div class="detail-item">
                              <span class="detail-label">Private Dirty</span>
                              <span class="detail-value mono">{{ formatMB(w.private_dirty_kb) }}</span>
                            </div>
                            <div class="detail-item">
                              <span class="detail-label">Growth (5min)</span>
                              <span class="detail-value mono">{{ formatGrowth(w.growth_5min_kb) }}</span>
                            </div>
                          </div>
                        </div>
                        <div class="detail-section">
                          <h4>System</h4>
                          <div class="detail-items">
                            <div class="detail-item">
                              <span class="detail-label">Available</span>
                              <span class="detail-value mono">{{ w.system_available_mb }} MB</span>
                            </div>
                            <div class="detail-item">
                              <span class="detail-label">Uptime</span>
                              <span class="detail-value mono">{{ formatDuration(w.uptime_sec) }}</span>
                            </div>
                            <div class="detail-item">
                              <span class="detail-label">Platform</span>
                              <span class="detail-value">{{ w.platform }}</span>
                            </div>
                            <div class="detail-item">
                              <span class="detail-label">Version</span>
                              <span class="detail-value">{{ w.version }}</span>
                            </div>
                          </div>
                        </div>
                        <div class="detail-section detail-section-full">
                          <h4>Reason</h4>
                          <p class="detail-reason">{{ w.reason }}</p>
                        </div>
                        <div class="detail-section detail-section-full">
                          <h4>Device</h4>
                          <span class="mono">{{ w.device_id }}</span>
                          <button class="filter-device-btn" @click.stop="filterByDevice(w.device_id)">
                            View all data for this device
                          </button>
                        </div>
                      </div>
                    </td>
                  </tr>
                </template>
              </tbody>
            </table>
          </div>
        </div>
      </template>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue'
import type { ChartOptions } from 'chart.js'
import DashSection from '@/components/DashSection.vue'
import BarChart from '@/components/BarChart.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import DistributionBar from '@/components/DistributionBar.vue'
import LineChart from '@/components/LineChart.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { formatDuration, formatMB, formatTimestamp, shortDeviceId } from '@/utils/format'
import { todayUtc, trimPartialDays } from '@/utils/series'
import { ungrouped } from '@/utils/versions'

const { data, loading, error, span } = useDashData(api.getMemoryWarnings)
const expandedRow = ref<number | null>(null)

function toggleRow(idx: number) {
  expandedRow.value = expandedRow.value === idx ? null : idx
}

function filterByDevice(deviceId: string) {
  // The device_id filter isn't in the standard filter bar, so copy it to the
  // clipboard and show a note.
  navigator.clipboard.writeText(deviceId)
  alert(`Device ID copied to clipboard:\n${deviceId}\n\nUse this to search telemetry logs for this device.`)
}

const stackedBarOpts: ChartOptions<'bar'> = {
  scales: {
    x: { stacked: true },
    y: { stacked: true }
  }
}

function formatGrowth(kb: number): string {
  const sign = kb >= 0 ? '+' : ''
  return `${sign}${(kb / 1024).toFixed(1)} MB`
}

const criticalCount = computed(() => {
  if (!data.value?.by_level) return 0
  const crit = data.value.by_level.find(l => l.level === 'critical')
  return crit?.count ?? 0
})

const avgRssAtWarning = computed(() => {
  if (!data.value?.rss_at_warning.length) return 0
  const sum = data.value.rss_at_warning.reduce((a, d) => a + d.avg_rss_kb, 0)
  return sum / data.value.rss_at_warning.length
})

const LEVEL_COLORS: Record<string, { border: string; bg: string }> = {
  elevated: { border: '#3b82f6', bg: 'rgba(59, 130, 246, 0.7)' },
  warning: { border: '#f59e0b', bg: 'rgba(245, 158, 11, 0.7)' },
  critical: { border: '#ef4444', bg: 'rgba(239, 68, 68, 0.7)' }
}

const warningsOverTimeChartData = computed(() => {
  const rows = data.value?.over_time ?? []
  const dates = trimPartialDays(
    [...new Set(rows.map(r => r.date))].sort().map(date => ({ date })),
    todayUtc()
  ).map(d => d.date)

  const levels = ['elevated', 'warning', 'critical']
  const datasets = levels.map(level => {
    const byDate = new Map<string, number>()
    for (const r of rows) {
      if (r.level === level) byDate.set(r.date, r.count)
    }
    const colors = LEVEL_COLORS[level] ?? { border: '#888', bg: 'rgba(136,136,136,0.7)' }
    return {
      label: level.charAt(0).toUpperCase() + level.slice(1),
      data: dates.map(d => byDate.get(d) ?? 0),
      borderColor: colors.border,
      backgroundColor: colors.bg
    }
  })

  return { labels: dates, datasets }
})

const rssAtWarning = computed(() => trimPartialDays(data.value?.rss_at_warning ?? [], todayUtc()))

const rssAtWarningChartData = computed(() => ({
  labels: rssAtWarning.value.map(d => d.date),
  datasets: [
    {
      label: 'Avg RSS (KB)',
      data: rssAtWarning.value.map(d => d.avg_rss_kb),
      borderColor: '#3b82f6',
      backgroundColor: 'rgba(59, 130, 246, 0.1)',
      fill: true,
      tension: 0.3
    },
    {
      label: 'Max RSS (KB)',
      data: rssAtWarning.value.map(d => d.max_rss_kb),
      borderColor: '#ef4444',
      backgroundColor: 'rgba(239, 68, 68, 0.1)',
      fill: false,
      tension: 0.3
    }
  ]
}))
</script>

<style scoped>
.section-subtitle {
  font-size: 13px;
  color: var(--text-secondary);
  margin: -8px 0 16px;
}
.table-container {
  overflow-x: auto;
  border: 1px solid var(--border);
  border-radius: 8px;
}

.warnings-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;
}

.warnings-table th {
  text-align: left;
  padding: 10px 12px;
  font-weight: 500;
  color: var(--text-secondary);
  background: var(--bg-card);
  border-bottom: 1px solid var(--border);
  white-space: nowrap;
}

.warnings-table td {
  padding: 8px 12px;
  border-bottom: 1px solid var(--border);
  vertical-align: top;
}

.warnings-table tr:last-child td {
  border-bottom: none;
}

.warnings-table tr:hover td {
  background: rgba(59, 130, 246, 0.04);
}



.warning-row {
  cursor: pointer;
  transition: background 0.1s;
}

.warning-row.expanded td {
  background: rgba(59, 130, 246, 0.06);
  border-bottom-color: transparent;
}

.expand-icon {
  display: inline-block;
  width: 12px;
  font-size: 10px;
  color: var(--text-secondary);
  margin-right: 4px;
}

.reason-cell {
  max-width: 300px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.detail-row td {
  padding: 0 12px 12px;
  background: rgba(59, 130, 246, 0.03);
}

.detail-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
  padding: 12px 0;
}

.detail-section {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: 6px;
  padding: 12px;
}

.detail-section-full {
  grid-column: 1 / -1;
}

.detail-section h4 {
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--text-secondary);
  margin-bottom: 8px;
}

.detail-items {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 6px;
}

.detail-item {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  font-size: 13px;
}

.detail-label {
  color: var(--text-secondary);
}

.detail-value {
  font-weight: 500;
}

.detail-reason {
  font-size: 13px;
  line-height: 1.5;
  word-break: break-word;
  white-space: normal;
}

.filter-device-btn {
  display: inline-block;
  margin-left: 12px;
  padding: 4px 10px;
  background: transparent;
  border: 1px solid var(--accent-blue);
  border-radius: 4px;
  color: var(--accent-blue);
  font-size: 12px;
  cursor: pointer;
  transition: background 0.15s, color 0.15s;
}

.filter-device-btn:hover {
  background: var(--accent-blue);
  color: #fff;
}

.level-badge {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.level-elevated {
  background: rgba(59, 130, 246, 0.15);
  color: #60a5fa;
}

.level-warning {
  background: rgba(245, 158, 11, 0.15);
  color: #fbbf24;
}

.level-critical {
  background: rgba(239, 68, 68, 0.15);
  color: #f87171;
}
</style>
