<template>
  <AppLayout>
    <div class="page">
      <div class="page-header">
        <h2>Report {{ week }}</h2>
      </div>

      <p v-if="missing" class="empty-state">No report for {{ week }}</p>
      <template v-else>
        <HealthReportBody :data="report?.health ?? null" :loading="loading" :error="error" />

        <DashSection title="Findings" :loading="loading" :error="error">
          <template v-if="report">
            <ChartCaption :text="`Generated ${report.generated_at}`" />
            <NoData v-if="report.findings.length === 0" />
            <div v-else class="table-wrapper">
              <table class="data-table">
                <thead>
                  <tr>
                    <th>Kind</th>
                    <th>Title</th>
                    <th>Counts</th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="f in report.findings" :key="f.fingerprint">
                    <td><span class="badge category">{{ f.kind }}</span></td>
                    <td>{{ f.title }}</td>
                    <td class="mono">{{ counts(f.summary) }}</td>
                  </tr>
                </tbody>
              </table>
            </div>
          </template>
        </DashSection>

        <DashSection title="Issues" :loading="loading" :error="error">
          <template v-if="report">
            <p v-if="report.issues_not_synced" class="empty-state">Issues were not synced: {{ report.issues_not_synced }}.</p>
            <NoData v-else-if="report.issues.length === 0" />
            <div v-else class="table-wrapper">
              <table class="data-table">
                <thead>
                  <tr>
                    <th>Action</th>
                    <th>Finding</th>
                    <th>Issue</th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="i in report.issues" :key="i.fingerprint">
                    <td><span class="badge">{{ i.action }}</span></td>
                    <td>{{ titles.get(i.fingerprint) ?? '-' }}</td>
                    <td>
                      <a v-if="i.issue" :href="i.issue.url" target="_blank" rel="noopener">#{{ i.issue.number }}</a>
                      <span v-else>-</span>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          </template>
        </DashSection>
      </template>
    </div>
  </AppLayout>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import AppLayout from '@/components/AppLayout.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import DashSection from '@/components/DashSection.vue'
import HealthReportBody from '@/components/HealthReportBody.vue'
import NoData from '@/components/NoData.vue'
import { useLoaded } from '@/composables/useLoaded'
import { api } from '@/services/api'

const route = useRoute()
const week = computed(() => String(route.params.week))
const { data: report, loading, error, missing } = useLoaded((w: string) => api.getReport(w), () => week.value)

const titles = computed(() => new Map((report.value?.findings ?? []).map((f) => [f.fingerprint, f.title])))

function counts(summary: Record<string, string | number>): string {
  return Object.entries(summary)
    .map(([k, v]) => `${k} ${typeof v === 'number' ? v.toLocaleString() : v}`)
    .join(', ')
}
</script>
