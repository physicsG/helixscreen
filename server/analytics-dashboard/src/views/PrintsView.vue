<template>
  <AppLayout>
    <div class="page">
      <div class="page-header">
        <h2>Prints</h2>
      </div>

      <PrintSuccessSection />
      <PrintsByModelSection />
      <PrintsByVersionSection />
      <MaterialSetsSection />
      <PrintStartSection />
    </div>
  </AppLayout>
</template>

<script setup lang="ts">
import AppLayout from '@/components/AppLayout.vue'
import PrintSuccessSection from '@/components/sections/PrintSuccessSection.vue'
import PrintsByModelSection from '@/components/sections/PrintsByModelSection.vue'
import PrintsByVersionSection from '@/components/sections/PrintsByVersionSection.vue'
import MaterialSetsSection from '@/components/sections/MaterialSetsSection.vue'
import PrintStartSection from '@/components/sections/PrintStartSection.vue'
</script>
