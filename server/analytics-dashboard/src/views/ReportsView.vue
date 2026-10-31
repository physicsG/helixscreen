<template>
  <AppLayout>
    <div class="page">
      <div class="page-header">
        <h2>Reports</h2>
      </div>

      <DashSection title="Weekly reports" :loading="loading" :error="error">
        <ChartCaption text="Snapshots stored when each week's report was generated, newest first" />
        <NoData v-if="!data || data.length === 0" />
        <div v-else class="table-wrapper">
          <table class="data-table">
            <thead>
              <tr>
                <th>Week</th>
              </tr>
            </thead>
            <tbody>
              <tr
                v-for="w in data"
                :key="w"
                class="clickable-row"
                title="click to open this report"
                @click="router.push(`/reports/${w}`)"
              >
                <td><router-link :to="`/reports/${w}`" class="mono" @click.stop>{{ w }}</router-link></td>
              </tr>
            </tbody>
          </table>
        </div>
      </DashSection>
    </div>
  </AppLayout>
</template>

<script setup lang="ts">
import { useRouter } from 'vue-router'
import AppLayout from '@/components/AppLayout.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import DashSection from '@/components/DashSection.vue'
import NoData from '@/components/NoData.vue'
import { api } from '@/services/api'
import { useLoaded } from '@/composables/useLoaded'

const router = useRouter()
const { data, loading, error } = useLoaded(() => api.getReports())
</script>
