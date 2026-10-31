<template>
  <DashSection title="Platforms" :loading="loading" :error="error">
    <DistributionBar
      v-if="data"
      :rows="ungrouped(data.platforms)"
      filter-key="platform"
      title="Platform Distribution"
      :caption="`Distinct devices with a session, ${span}`"
    />
  </DashSection>
</template>

<script setup lang="ts">
import DashSection from '@/components/DashSection.vue'
import DistributionBar from '@/components/DistributionBar.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { ungrouped } from '@/utils/versions'

const { data, loading, error, span } = useDashData(api.getAdoption)
</script>
