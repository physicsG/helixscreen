<template>
  <DashSection title="Versions" :loading="loading" :error="error">
    <DistributionBar
      v-if="data"
      :rows="groupVersions(data.versions, new Set(filters.expand))"
      filter-key="version"
      title="Version Distribution"
      :caption="`Distinct devices with a session, ${span}; older lines and unreleased betas are grouped, click one to expand it`"
    />
  </DashSection>
</template>

<script setup lang="ts">
import DashSection from '@/components/DashSection.vue'
import DistributionBar from '@/components/DistributionBar.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { groupVersions } from '@/utils/versions'

const { data, loading, error, span, filters } = useDashData(api.getAdoption)
</script>
