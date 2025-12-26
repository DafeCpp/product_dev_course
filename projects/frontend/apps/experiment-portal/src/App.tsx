import { Routes, Route, Navigate } from 'react-router-dom'
import Layout from './components/Layout'
import ProtectedRoute from './components/ProtectedRoute'
import Login from './pages/Login'
import ExperimentsList from './pages/ExperimentsList'
import ExperimentDetail from './pages/ExperimentDetail'
import CreateExperiment from './pages/CreateExperiment'
import RunDetail from './pages/RunDetail'
import SensorsList from './pages/SensorsList'
import SensorDetail from './pages/SensorDetail'
import CreateSensor from './pages/CreateSensor'
import './App.css'

function App() {
  return (
    <Routes>
      <Route path="/login" element={<Login />} />
      <Route
        path="/*"
        element={
          <ProtectedRoute>
            <Layout>
              <Routes>
                <Route path="/" element={<Navigate to="/experiments" replace />} />
                <Route path="/experiments" element={<ExperimentsList />} />
                <Route path="/experiments/new" element={<CreateExperiment />} />
                <Route path="/experiments/:id" element={<ExperimentDetail />} />
                <Route path="/runs/:id" element={<RunDetail />} />
                <Route path="/sensors" element={<SensorsList />} />
                <Route path="/sensors/new" element={<CreateSensor />} />
                <Route path="/sensors/:id" element={<SensorDetail />} />
              </Routes>
            </Layout>
          </ProtectedRoute>
        }
      />
    </Routes>
  )
}

export default App

