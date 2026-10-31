<template>
  <DashSection title="Features" :loading="loading" :error="error">
    <template v-if="data && engagement">
      <DeltaEraWaiting v-if="waiting" :era="data" />
      <template v-else>
        <div class="metrics-row">
          <MetricCard
            title="Devices on New Client"
            :value="data.delta_era_devices.toLocaleString()"
            :subtitle="`the adoption denominator, ${span}`"
            color="var(--accent-yellow)"
          />
          <MetricCard
            title="Features Used"
            :value="String(data.features.length)"
            color="var(--accent-blue)"
          />
          <MetricCard
            title="Most Used"
            :value="mostUsed"
            color="var(--accent-green)"
          />
          <MetricCard
            title="Least Used"
            :value="leastUsed"
            color="var(--accent-red)"
          />
        </div>

        <div class="chart-section">
          <h3>Overlay and Widget Adoption</h3>
          <ChartCaption :text="`Share of devices on the new client that used each at least once, ${span}`" />
          <NoData v-if="byAdoption.length === 0" :era="data" />
          <BarChart
            v-else
            :data="adoptionChartData"
            :options="adoptionOpts"
            :height="barChartHeight(byAdoption.length)"
          />
        </div>

        <div class="chart-section">
          <h3>Least Adopted</h3>
          <ChartCaption :text="`Every overlay and widget some device used, least adopted first, ${span}`" />
          <NoData v-if="byAdoption.length === 0" :era="data" />
          <div v-else class="table-wrapper">
            <table class="data-table">
              <thead>
                <tr><th>Feature</th><th>Adoption</th><th>Devices Using</th></tr>
              </thead>
              <tbody>
                <tr v-for="f in leastAdopted" :key="f.id">
                  <td>{{ titleCase(f.id) }}</td>
                  <td class="mono">{{ formatRate(f.adoption_rate) }}</td>
                  <td class="mono">{{ f.devices }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </template>

      <div class="chart-section">
        <h3>Widget Adoption (devices with widget enabled)</h3>
        <ChartCaption :text="`Distinct devices with each home widget placed, ${span}`" />
        <NoData v-if="!engagement.widget_placement?.length" :era="engagement" />
        <BarChart v-else :data="widgetPlacementChartData" :options="verticalWidgetBarOpts" />
      </div>

      <div class="chart-section">
        <h3>Widget Interactions</h3>
        <ChartCaption :text="`Sample-weighted taps on each home widget, ${span}`" />
        <DeltaEraWaiting v-if="engagementWaiting" :era="engagement" />
        <NoData v-else-if="!engagement.widget_interactions?.length" :era="engagement" />
        <BarChart v-else :data="widgetInteractionChartData" :options="verticalWidgetBarOpts" />
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { ChartOptions } from 'chart.js'
import DashSection from '@/components/DashSection.vue'
import BarChart from '@/components/BarChart.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import DeltaEraWaiting from '@/components/DeltaEraWaiting.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { rateAxis } from '@/utils/chart'
import { formatRate, titleCase } from '@/utils/format'
import { barChartHeight } from '@/utils/series'

const features = useDashData(api.getFeatures)
const eng = useDashData(api.getEngagement)
const data = features.data
const engagement = eng.data
const span = features.span
const loading = computed(() => features.loading.value || eng.loading.value)
const error = computed(() => features.error.value || eng.error.value)

const waiting = computed(() =>
  !!data.value && (data.value.delta_era_devices === 0 || data.value.filter_not_applicable !== null)
)
const engagementWaiting = computed(() =>
  !!engagement.value &&
  (engagement.value.delta_era_devices === 0 || engagement.value.filter_not_applicable !== null)
)

const byAdoption = computed(() =>
  [...(data.value?.features ?? [])].sort((a, b) => b.adoption_rate - a.adoption_rate)
)

// Only ids some device used are reported, so this is the tail of what was used.
const leastAdopted = computed(() => [...byAdoption.value].reverse())

const mostUsed = computed(() => {
  const f = byAdoption.value[0]
  return f ? `${titleCase(f.id)} (${formatRate(f.adoption_rate, 0)})` : 'N/A'
})

const leastUsed = computed(() => {
  const f = byAdoption.value[byAdoption.value.length - 1]
  return f ? `${titleCase(f.id)} (${formatRate(f.adoption_rate, 0)})` : 'N/A'
})

const adoptionOpts: ChartOptions<'bar'> = {
  indexAxis: 'y',
  scales: { x: { ...rateAxis(), max: 1 }, y: { ticks: { autoSkip: false } } },
  plugins: { tooltip: { callbacks: { label: (ctx) => formatRate(Number(ctx.raw)) } } }
}

const adoptionChartData = computed(() => ({
  labels: byAdoption.value.map(f => titleCase(f.id)),
  datasets: [{
    label: 'Adoption',
    data: byAdoption.value.map(f => f.adoption_rate),
    backgroundColor: '#3b82f6',
  }],
}))

const verticalWidgetBarOpts: ChartOptions<'bar'> = {
  scales: { x: { ticks: { autoSkip: false, maxRotation: 45, minRotation: 45 } } }
}

const widgetPlacementChartData = computed(() => ({
  labels: engagement.value?.widget_placement?.map(w => titleCase(w.widget)) ?? [],
  datasets: [{
    label: 'Devices',
    data: engagement.value?.widget_placement?.map(w => w.devices) ?? [],
    backgroundColor: '#10b981'
  }]
}))

const widgetInteractionChartData = computed(() => ({
  labels: engagement.value?.widget_interactions?.map(w => titleCase(w.widget)) ?? [],
  datasets: [{
    label: 'Interactions',
    data: engagement.value?.widget_interactions?.map(w => w.interactions) ?? [],
    backgroundColor: '#8b5cf6'
  }]
}))
</script>
