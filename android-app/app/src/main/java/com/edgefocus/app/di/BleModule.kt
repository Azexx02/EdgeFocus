package com.edgefocus.app.di

import android.content.Context
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.ble.BleHostConnectionTransport
import com.edgefocus.app.data.ble.HostConnectionTransport
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.android.qualifiers.ApplicationContext
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
object BleModule {

    @Provides
    @Singleton
    fun provideBleManager(@ApplicationContext context: Context): BleManager {
        return BleManager(context)
    }

    @Provides
    @Singleton
    fun provideHostConnectionTransport(
        transport: BleHostConnectionTransport
    ): HostConnectionTransport = transport
}
