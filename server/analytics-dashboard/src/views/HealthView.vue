<template>
  <AppLayout>
    <div class="page">
      <div class="page-header">
        <h2>Health</h2>
      </div>

      <HealthReportBody :data="data" :loading="loading" :error="error" />
    </div>
  </AppLayout>
</template>

<script setup lang="ts">
import AppLayout from '@/components/AppLayout.vue'
import HealthReportBody from '@/components/HealthReportBody.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'

const { data, loading, error } = useDashData(api.getHealth)
</script>
