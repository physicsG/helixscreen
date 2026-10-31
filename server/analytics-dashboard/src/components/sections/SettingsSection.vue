<template>
  <DashSection title="Settings" :loading="loading" :error="error">
    <template v-if="ux && features">
      <div class="metrics-row">
        <MetricCard
          title="Settings Changes/Device/Week"
          :value="ux.settings_change_rate_per_device_per_week.toFixed(1)"
          :subtitle="span"
          color="var(--accent-yellow)"
        />
      </div>

      <div class="chart-section">
        <h3>Most Changed Settings</h3>
        <ChartCaption :text="`Sample-weighted setting changes, ${span}`" />
        <NoData v-if="changes.length === 0" />
        <BarChart v-else :data="settingsChangesData" :options="horizontalBarOpts" :height="barChartHeight(changes.length)" />
      </div>

      <div class="chart-section">
        <h3>Settings Changed from Default</h3>
        <ChartCaption :text="`Share of devices that changed each setting at least once, ${span}`" />
        <NoData v-if="ux.settings_defaults.length === 0" />
        <div v-else class="table-wrapper">
          <table class="data-table">
            <thead>
              <tr><th>Setting</th><th>Changed</th><th>Devices</th></tr>
            </thead>
            <tbody>
              <tr v-for="s in ux.settings_defaults" :key="s.setting">
                <td>{{ titleCase(s.setting) }}</td>
                <td class="mono">{{ formatRate(s.pct_changed) }}</td>
                <td class="mono">{{ s.devices_changed }}</td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>

      <div class="chart-section">
        <h3>Configured in Settings</h3>
        <ChartCaption :text="`Devices with each feature on over devices reporting it, ${span}`" />
        <NoData v-if="features.configured.length === 0" :era="features" />
        <div v-else class="table-wrapper">
          <table class="data-table">
            <thead>
              <tr><th>Setting</th><th>Adoption</th><th>Devices On</th><th>Devices Reporting</th></tr>
            </thead>
            <tbody>
              <tr v-for="f in features.configured" :key="f.id">
                <td>{{ titleCase(f.id) }}</td>
                <td class="mono">{{ formatRate(f.adoption_rate) }}</td>
                <td class="mono">{{ f.devices }}</td>
                <td class="mono">{{ f.reported }}</td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import DashSection from '@/components/DashSection.vue'
import BarChart from '@/components/BarChart.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { horizontalBarOpts } from '@/utils/chart'
import { formatRate, titleCase } from '@/utils/format'
import { barChartHeight } from '@/utils/series'

const uxData = useDashData(api.getUxInsights)
const featureData = useDashData(api.getFeatures)
const ux = uxData.data
const features = featureData.data
const span = uxData.span
const loading = computed(() => uxData.loading.value || featureData.loading.value)
const error = computed(() => uxData.error.value || featureData.error.value)

const changes = computed(() =>
  [...(ux.value?.settings_changes ?? [])].sort((a, b) => b.change_count - a.change_count)
)

const settingsChangesData = computed(() => ({
  labels: changes.value.map(s => titleCase(s.setting)),
  datasets: [{
    label: 'Changes',
    data: changes.value.map(s => s.change_count),
    backgroundColor: '#8b5cf6',
  }],
}))
</script>
