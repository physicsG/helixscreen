<template>
  <DashSection title="Printer models" :loading="loading" :error="error">
    <DistributionBar
      v-if="data"
      :rows="canonicalModels(data.printer_models)"
      filter-key="model"
      title="Printer Models"
      :caption="`Distinct devices with a session, ${span}; spellings of one model are merged`"
    />
  </DashSection>
</template>

<script setup lang="ts">
import DashSection from '@/components/DashSection.vue'
import DistributionBar from '@/components/DistributionBar.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { canonicalModels } from '@/utils/models'

const { data, loading, error, span } = useDashData(api.getAdoption)
</script>
