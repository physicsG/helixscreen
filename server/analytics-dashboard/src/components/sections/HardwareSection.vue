<template>
  <DashSection title="Hardware" :loading="loading" :error="error">
    <template v-if="data">
      <ChartCaption :text="`Each device's latest hardware profile, ${span}`" />
      <div class="metrics-row">
        <MetricCard
          title="Avg Build Volume"
          :value="formatVolume(data.avg_build_volume)"
          color="var(--accent-blue)"
        />
        <MetricCard
          title="Avg Fan Count"
          :value="data.avg_counts.fans.toFixed(1)"
          color="var(--accent-green)"
        />
        <MetricCard
          title="Avg Sensor Count"
          :value="data.avg_counts.sensors.toFixed(1)"
          color="var(--accent-yellow)"
        />
        <MetricCard
          title="Avg Macro Count"
          :value="data.avg_counts.macros.toFixed(0)"
          color="var(--accent-purple)"
        />
      </div>

      <div class="chart-section">
        <h3>Kinematics Breakdown</h3>
        <ChartCaption :text="`Distinct devices with a hardware profile, ${span}`" />
        <NoData v-if="data.kinematics.length === 0" />
        <PieChart v-else :data="kinematicsChartData" />
      </div>

      <div class="chart-section">
        <h3>MCU Chip Distribution</h3>
        <ChartCaption :text="`Distinct devices with a hardware profile, ${span}`" />
        <NoData v-if="data.mcu_chips.length === 0" />
        <BarChart v-else :data="mcuChartData" :options="horizontalBarOpts" :height="barChartHeight(data.mcu_chips.length)" />
      </div>

      <div class="chart-section">
        <h3>Capability Adoption</h3>
        <ChartCaption :text="`Share of devices whose latest hardware profile has each capability, ${span}`" />
        <NoData v-if="capabilityChartData.labels.length === 0" />
        <BarChart
          v-else
          :data="capabilityChartData"
          :options="percentHorizontalOpts"
          :height="barChartHeight(capabilityChartData.labels.length)"
        />
      </div>

      <div class="chart-section">
        <h3>Host RAM Distribution</h3>
        <ChartCaption :text="`Distinct devices with a hardware profile, ${span}`" />
        <NoData v-if="data.ram_distribution.length === 0" />
        <BarChart v-else :data="ramChartData" :options="horizontalBarOpts" :height="barChartHeight(data.ram_distribution.length)" />
      </div>

      <div class="chart-section" v-if="data.ams_backends.length > 0">
        <h3>AMS Backend Distribution</h3>
        <ChartCaption :text="`Distinct devices with a hardware profile, ${span}`" />
        <BarChart :data="amsChartData" :options="horizontalBarOpts" :height="barChartHeight(data.ams_backends.length)" />
      </div>

      <div class="split-row">
        <div class="chart-section">
          <h3>Helix Macros Installed</h3>
          <ChartCaption :text="`Devices reporting whether helix_macros.cfg is installed, ${span}`" />
          <template v-if="helixMacros.reported > 0">
            <PieChart :data="helixMacrosChartData" />
            <p class="chart-note">
              {{ formatRate(helixMacros.installed / helixMacros.reported, 0) }} of
              {{ helixMacros.reported }} reporting devices have
              <code>helix_macros.cfg</code>
            </p>
          </template>
          <p v-else class="chart-note empty">No devices have reported this yet.</p>
        </div>

        <div class="chart-section">
          <h3>Moonraker Topology</h3>
          <ChartCaption :text="`Devices reporting where Moonraker runs, ${span}`" />
          <template v-if="moonrakerLocality.reported > 0">
            <PieChart :data="moonrakerLocalityChartData" />
            <p class="chart-note">
              {{ formatRate(moonrakerLocality.remote / moonrakerLocality.reported, 0) }} of
              {{ moonrakerLocality.reported }} reporting devices drive Moonraker
              over the network
            </p>
          </template>
          <!-- The day-one state. An empty chart here would read as "0% remote",
               which is a conclusion rather than an absence of data. -->
          <p v-else class="chart-note empty">
            No devices have reported this yet. Requires a client release.
          </p>
        </div>
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { ChartOptions } from 'chart.js'
import DashSection from '@/components/DashSection.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import MetricCard from '@/components/MetricCard.vue'
import BarChart from '@/components/BarChart.vue'
import PieChart from '@/components/PieChart.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { horizontalBarOpts, rateAxis } from '@/utils/chart'
import { formatRate } from '@/utils/format'
import { barChartHeight, seriesColor } from '@/utils/series'

const { data, loading, error, span } = useDashData(api.getHardware)

const percentHorizontalOpts: ChartOptions<'bar'> = {
  indexAxis: 'y',
  scales: {
    x: { ...rateAxis(), max: 1 },
    y: { ticks: { autoSkip: false } }
  },
  plugins: {
    tooltip: { callbacks: { label: (ctx) => formatRate(Number(ctx.raw)) } }
  }
}

function formatVolume(vol: { x: number; y: number; z: number }): string {
  return `${Math.round(vol.x)}x${Math.round(vol.y)}x${Math.round(vol.z)} mm`
}

const kinematicsChartData = computed(() => ({
  labels: data.value?.kinematics.map(k => k.name) ?? [],
  datasets: [{
    data: data.value?.kinematics.map(k => k.count) ?? [],
    backgroundColor: data.value?.kinematics.map((_, i) => seriesColor(i)) ?? []
  }]
}))

const mcuChartData = computed(() => ({
  labels: data.value?.mcu_chips.map(m => m.name) ?? [],
  datasets: [{
    label: 'Devices',
    data: data.value?.mcu_chips.map(m => m.count) ?? [],
    backgroundColor: '#3b82f6'
  }]
}))

const CAP_NAMES = [
  'Has Chamber', 'Has Accelerometer', 'Has Firmware Retraction', 'Has Exclude Object',
  'Has Timelapse', 'Has Klippain ShakeTune', 'Has Speaker',
  'Has Probe', 'Has LED', 'Has Filament Sensor', 'Has Multi Extruder', 'Has AMS', 'Has Heater Bed'
]

const capabilityChartData = computed(() => {
  const caps = data.value?.capabilities
  if (!caps || !caps.total) return { labels: [] as string[], datasets: [] }
  return {
    labels: CAP_NAMES.slice(0, caps.bits.length),
    datasets: [{
      label: 'Adoption',
      data: caps.bits.map(count => count / caps.total),
      backgroundColor: '#8b5cf6'
    }]
  }
})

const ramChartData = computed(() => ({
  labels: data.value?.ram_distribution.map(r => r.name) ?? [],
  datasets: [{
    label: 'Devices',
    data: data.value?.ram_distribution.map(r => r.count) ?? [],
    backgroundColor: '#06b6d4'
  }]
}))

const amsChartData = computed(() => ({
  labels: data.value?.ams_backends.map(a => a.name) ?? [],
  datasets: [{
    label: 'Devices',
    data: data.value?.ams_backends.map(a => a.count) ?? [],
    backgroundColor: '#f59e0b'
  }]
}))

// Defensive defaults: the dashboard can be served against a worker that predates
// these fields, and an undefined here would blank the whole view.
const ZERO_SPLIT = { installed: 0, not_installed: 0, local: 0, remote: 0, reported: 0 }

const helixMacros = computed(() => data.value?.helix_macros ?? ZERO_SPLIT)
const moonrakerLocality = computed(() => data.value?.moonraker_locality ?? ZERO_SPLIT)

const helixMacrosChartData = computed(() => ({
  labels: ['Installed', 'Not installed'],
  datasets: [{
    data: [helixMacros.value.installed, helixMacros.value.not_installed],
    backgroundColor: ['#10b981', '#64748b']
  }]
}))

const moonrakerLocalityChartData = computed(() => ({
  labels: ['On the printer', 'Over the network'],
  datasets: [{
    data: [moonrakerLocality.value.local, moonrakerLocality.value.remote],
    backgroundColor: ['#3b82f6', '#f59e0b']
  }]
}))
</script>

<style scoped>
.split-row {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 16px;
}

.chart-note {
  margin-top: 12px;
  font-size: 13px;
  color: var(--text-secondary);
}

.chart-note.empty {
  padding: 24px 0;
  text-align: center;
}

.chart-note code {
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  font-size: 12px;
}
</style>
