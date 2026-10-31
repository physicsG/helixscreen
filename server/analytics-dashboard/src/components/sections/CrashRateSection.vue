<template>
  <DashSection title="Crash rate" :loading="loading" :error="error">
    <template v-if="data && stability">
      <!-- 1.0 readiness headline: trailing 14-day session crash rate against the 1.5% target -->
      <div class="metrics-row">
        <MetricCard
          title="Crash Rate"
          :value="formatRate(overallCrashRate)"
          :subtitle="`of sessions, ${span}`"
          color="var(--accent-red)"
        />
      </div>
      <div v-if="data.trailing_14d" class="gauge-section">
        <div class="gauge-card" :class="gaugeStatus">
          <div class="gauge-label">Trailing 14-day session crash rate</div>
          <div class="gauge-value">{{ formatRate(data.trailing_14d.rate, 2) }}</div>
          <div class="gauge-sub">
            {{ data.trailing_14d.crash_count.toLocaleString() }} crashes
            /
            {{ data.trailing_14d.session_count.toLocaleString() }} sessions
            <span class="gauge-threshold">
              · target: &lt; 1.50% for 1.0
            </span>
          </div>
        </div>
      </div>

      <!-- Per-platform breakdown: core platforms vs small-N platforms -->
      <div v-if="corePlatforms.length" class="chart-section">
        <h3>Core platforms (≥ 100 sessions)</h3>
        <ChartCaption :text="`Sample-weighted crashes over sessions per platform, ${span}; click a card to filter`" />
        <div class="platform-grid">
          <button
            v-for="p in corePlatforms"
            :key="p.platform"
            class="platform-card"
            :class="platformRateClass(p.rate)"
            :title="rowHint(single(p.platform))"
            @click="drillPlatform(p.platform)"
          >
            <div class="platform-name">{{ p.platform }}</div>
            <div class="platform-rate">{{ formatRate(p.rate, 2) }}</div>
            <div class="platform-sub">
              {{ p.crash_count }} crashes / {{ p.session_count.toLocaleString() }} sessions
              <br>
              {{ p.crashing_devices }} of {{ p.session_devices }} devices crashed
            </div>
          </button>
        </div>
      </div>
      <div v-if="limitedPlatforms.length" class="chart-section">
        <h3>
          Limited-data platforms (&lt; 100 sessions)
          <span class="hint">small N; rates may be noisy</span>
        </h3>
        <ChartCaption :text="`Sample-weighted crashes over sessions per platform, ${span}; click a card to filter`" />
        <div class="platform-grid">
          <button
            v-for="p in limitedPlatforms"
            :key="p.platform"
            class="platform-card limited"
            :title="rowHint(single(p.platform))"
            @click="drillPlatform(p.platform)"
          >
            <div class="platform-name">{{ p.platform }}</div>
            <div class="platform-rate">{{ formatRate(p.rate) }}</div>
            <div class="platform-sub">
              {{ p.crash_count }} crashes / {{ p.session_count }} sessions
              <br>
              {{ p.crashing_devices }} of {{ p.session_devices }} devices crashed
            </div>
          </button>
        </div>
      </div>

      <div class="chart-section">
        <h3>Crash Rate Over Time</h3>
        <ChartCaption :text="`Sample-weighted crashes over sessions per day, ${span}; first and current day omitted`" />
        <NoData v-if="crashesUnreported || trend.length === 0" :not-applicable="crashesUnreported" />
        <LineChart v-else :data="crashRateChartData" :options="crashRateOpts" />
      </div>

      <div class="chart-section">
        <h3>Crash Rate by Version</h3>
        <ChartCaption :text="`Sample-weighted crashes over sessions per version, ${span}; older lines and unreleased betas are grouped, their rate recombined from counts; click a bar to expand or filter`" />
        <NoData v-if="byVersion.length === 0" :not-applicable="crashesUnreported" />
        <BarChart
          v-else
          :data="versionChartData"
          :options="versionOpts"
          :height="barChartHeight(byVersion.length)"
          @select="(i) => drillVersion(byVersion[i])"
        />
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRouter } from 'vue-router'
import type { ChartOptions } from 'chart.js'
import DashSection from '@/components/DashSection.vue'
import BarChart from '@/components/BarChart.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import LineChart from '@/components/LineChart.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { rateAxis } from '@/utils/chart'
import { rowHint, rowLabel, single } from '@/utils/drill'
import { formatRate } from '@/utils/format'
import { barChartHeight, todayUtc, trimPartialDays } from '@/utils/series'
import { groupVersionRates, type Grouped } from '@/utils/versions'

const crashes = useDashData(api.getCrashes)
const stab = useDashData(api.getStability)
const data = crashes.data
const stability = stab.data
const span = crashes.span
const loading = computed(() => crashes.loading.value || stab.loading.value)
const error = computed(() => crashes.error.value || stab.error.value)

const router = useRouter()
const filters = crashes.filters

function drillPlatform(platform: string) {
  filters.activate(router, [['platform', single(platform)]])
}

function drillVersion(row: Grouped | undefined) {
  if (row) filters.activate(router, [['version', row]])
}

/** The active filter crash rows do not carry, or null. */
const crashesUnreported = computed(() => stability.value?.not_applicable.crashes ?? null)

// 0-1 over the whole window, or null when the filters make the crash rate
// inapplicable or there are no sessions to divide by.
const overallCrashRate = computed((): number | null => {
  const all = stability.value?.crash_rate_trend ?? []
  if (all.some(d => d.rate === null)) return null
  const crashCount = all.reduce((sum, d) => sum + d.crashes, 0)
  const sessions = all.reduce((sum, d) => sum + d.sessions, 0)
  return sessions > 0 ? crashCount / sessions : null
})

const trend = computed(() => trimPartialDays(stability.value?.crash_rate_trend ?? [], todayUtc()))

const crashRateOpts = computed((): ChartOptions<'line'> => ({
  scales: { y: rateAxis(1) },
  plugins: {
    tooltip: {
      callbacks: {
        label: (ctx) => {
          const point = trend.value[ctx.dataIndex]
          if (point) {
            return `Rate: ${formatRate(point.rate, 2)} (${point.crashes} crashes / ${point.sessions} sessions)`
          }
          return formatRate(Number(ctx.raw), 2)
        }
      }
    }
  }
}))

const crashRateChartData = computed(() => ({
  labels: trend.value.map(d => d.date),
  datasets: [{
    label: 'Crash Rate',
    data: trend.value.map(d => d.rate),
    borderColor: '#ef4444',
    backgroundColor: 'rgba(239, 68, 68, 0.1)',
    fill: true,
    tension: 0.3
  }]
}))

const byVersion = computed(() =>
  groupVersionRates(
    (data.value?.by_version ?? []).map((v) => ({
      version: v.version,
      numerator: v.crash_count,
      denominator: v.session_count,
      rate: v.rate
    })),
    new Set(filters.expand)
  )
)

const versionOpts = computed((): ChartOptions<'bar'> => ({
  indexAxis: 'y',
  scales: { x: rateAxis(1), y: { ticks: { autoSkip: false } } },
  plugins: {
    legend: { display: false },
    tooltip: {
      callbacks: {
        label: (ctx) => {
          const v = byVersion.value[ctx.dataIndex]
          return v ? `${formatRate(v.rate, 2)} (${v.numerator} crashes / ${v.denominator} sessions)` : ''
        },
        afterLabel: (ctx) => rowHint(byVersion.value[ctx.dataIndex])
      }
    }
  }
}))

const versionChartData = computed(() => ({
  labels: byVersion.value.map(rowLabel),
  datasets: [{
    label: 'Crash Rate',
    data: byVersion.value.map(v => v.rate),
    backgroundColor: '#ef4444'
  }]
}))

const GAUGE_GREEN = 0.015 // < 1.5% sessions: green
const GAUGE_YELLOW = 0.03 // 1.5%-3% sessions: yellow, above: red

const gaugeStatus = computed(() => {
  const r = data.value?.trailing_14d?.rate ?? null
  if (r === null) return ''
  if (r < GAUGE_GREEN) return 'gauge-green'
  if (r < GAUGE_YELLOW) return 'gauge-yellow'
  return 'gauge-red'
})

function platformRateClass(rate: number | null): string {
  if (rate === null) return ''
  if (rate < GAUGE_GREEN) return 'rate-green'
  if (rate < GAUGE_YELLOW) return 'rate-yellow'
  return 'rate-red'
}

// Split platforms by sample size so small-N data does not visually dominate
// the core fleet's rate.
const SMALL_N_THRESHOLD = 100
const corePlatforms = computed(() =>
  (data.value?.by_platform ?? []).filter(p => p.session_count >= SMALL_N_THRESHOLD),
)
const limitedPlatforms = computed(() =>
  (data.value?.by_platform ?? []).filter(p => p.session_count < SMALL_N_THRESHOLD),
)
</script>

<style scoped>
.gauge-section {
  margin-bottom: 24px;
}

.gauge-card {
  padding: 20px 24px;
  border: 1px solid var(--border);
  border-left-width: 6px;
  border-radius: 8px;
  background: var(--bg-card);
}

.gauge-card.gauge-green {
  border-left-color: #10b981;
}

.gauge-card.gauge-yellow {
  border-left-color: #f59e0b;
}

.gauge-card.gauge-red {
  border-left-color: #ef4444;
}

.gauge-label {
  font-size: 13px;
  color: var(--text-secondary);
  font-weight: 500;
}

.gauge-value {
  font-size: 42px;
  font-weight: 700;
  margin: 4px 0 8px;
  font-family: 'SF Mono', 'Fira Code', monospace;
}

.gauge-card.gauge-green .gauge-value {
  color: #10b981;
}

.gauge-card.gauge-yellow .gauge-value {
  color: #f59e0b;
}

.gauge-card.gauge-red .gauge-value {
  color: #ef4444;
}

.gauge-sub {
  font-size: 12px;
  color: var(--text-secondary);
}

.gauge-threshold {
  margin-left: 6px;
  font-style: italic;
}

.platform-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 12px;
}

.platform-card {
  padding: 14px 16px;
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-left-width: 4px;
  border-radius: 6px;
  text-align: left;
  color: inherit;
  font: inherit;
}

.platform-card:hover {
  border-color: var(--accent-blue);
}

.platform-card.rate-green { border-left-color: #10b981; }
.platform-card.rate-yellow { border-left-color: #f59e0b; }
.platform-card.rate-red { border-left-color: #ef4444; }

.platform-card.limited {
  border-left-color: #64748b;
  opacity: 0.85;
}

.platform-name {
  font-size: 13px;
  font-weight: 600;
  color: var(--text-primary);
  text-transform: uppercase;
  letter-spacing: 0.04em;
}

.platform-rate {
  font-size: 24px;
  font-weight: 700;
  margin: 2px 0 6px;
  font-family: 'SF Mono', 'Fira Code', monospace;
}

.platform-card.rate-green .platform-rate { color: #10b981; }
.platform-card.rate-yellow .platform-rate { color: #f59e0b; }
.platform-card.rate-red .platform-rate { color: #ef4444; }
.platform-card.limited .platform-rate { color: var(--text-secondary); }

.platform-sub {
  font-size: 11px;
  color: var(--text-secondary);
  line-height: 1.4;
}

.chart-section h3 .hint {
  font-weight: 400;
  font-size: 12px;
  color: var(--text-secondary);
  margin-left: 8px;
}
</style>
