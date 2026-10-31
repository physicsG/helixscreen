<template>
  <AppLayout>
    <div class="page">
      <div class="page-header">
        <h2>Performance</h2>
      </div>

      <FrameTimeSection />
      <DropRateSection />
      <PlatformVersionPerfSection />
      <JankiestPanelsSection />
      <RssTrendSection />
    </div>
  </AppLayout>
</template>

<script setup lang="ts">
import AppLayout from '@/components/AppLayout.vue'
import FrameTimeSection from '@/components/sections/FrameTimeSection.vue'
import DropRateSection from '@/components/sections/DropRateSection.vue'
import PlatformVersionPerfSection from '@/components/sections/PlatformVersionPerfSection.vue'
import JankiestPanelsSection from '@/components/sections/JankiestPanelsSection.vue'
import RssTrendSection from '@/components/sections/RssTrendSection.vue'
</script>
